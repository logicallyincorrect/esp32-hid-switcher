"""Small, version-checked build-local correction to ESP-IDF's root hub driver."""

ROOT_DISCONNECT = '''static void root_port_notify_device_gone(hcd_port_handle_t root_port_hdl)
{
    const esp_err_t ret = dev_tree_node_dev_gone(NULL, 0);
    if (ret == ESP_ERR_NOT_FOUND) {
        // Disconnect/power-off can arrive after failed enumeration has already
        // removed the root node. There is no device left to notify or free.
        // Recover the HCD if necessary; root_port_req preserves explicit power-off.
        if (hcd_port_get_state(root_port_hdl) == HCD_PORT_STATE_RECOVERY) {
            HUB_DRIVER_ENTER_CRITICAL();
            p_hub_driver_obj->dynamic.port_reqs |= PORT_REQ_RECOVER;
            p_hub_driver_obj->dynamic.flags.actions |= HUB_DRIVER_ACTION_ROOT_REQ;
            HUB_DRIVER_EXIT_CRITICAL();
        }
    } else {
        ESP_ERROR_CHECK(ret);
    }
}

'''

ROOT_STOP = '''esp_err_t hub_root_stop(void)
{
    HUB_DRIVER_ENTER_CRITICAL();
    HUB_DRIVER_CHECK_FROM_CRIT(p_hub_driver_obj != NULL, ESP_ERR_INVALID_STATE);
    const root_port_state_t previous = p_hub_driver_obj->dynamic.root_port_state;
    if (previous == ROOT_PORT_STATE_NOT_POWERED) {
        HUB_DRIVER_EXIT_CRITICAL();
        return ESP_OK;
    }
    p_hub_driver_obj->dynamic.root_port_state = ROOT_PORT_STATE_NOT_POWERED;
    HUB_DRIVER_EXIT_CRITICAL();

    // An interrupt may have queued a port event since the last host dispatch.
    // HCD rejects commands until it is handled. Preserve state and let the
    // host task dispatch the event before retrying, rather than asserting.
    const esp_err_t ret = hcd_port_command(p_hub_driver_obj->constant.root_port_hdl, HCD_PORT_CMD_POWER_OFF);
    if (ret != ESP_OK) {
        HUB_DRIVER_ENTER_CRITICAL();
        if (p_hub_driver_obj->dynamic.root_port_state == ROOT_PORT_STATE_NOT_POWERED) {
            p_hub_driver_obj->dynamic.root_port_state = previous;
        }
        HUB_DRIVER_EXIT_CRITICAL();
    }
    return ret;
}
'''


def patch_hub_source(source):
    anchor = "static void root_port_handle_events(hcd_port_handle_t root_port_hdl)\n"
    old = "            ESP_ERROR_CHECK(dev_tree_node_dev_gone(NULL, 0));"
    if source.count(anchor) != 1 or source.count(old) != 1:
        raise RuntimeError("USB SDK root-hub source changed; review the disconnect patch")
    stop_start = source.index("esp_err_t hub_root_stop(void)\n")
    stop_end = source.index("\n}\n", stop_start) + 3
    if "assert(ret == ESP_OK);" not in source[stop_start:stop_end]:
        raise RuntimeError("USB SDK power-off source changed; review the retry patch")
    source = source[:stop_start] + ROOT_STOP + source[stop_end:]
    return source.replace(anchor, ROOT_DISCONNECT + anchor).replace(
        old, "            root_port_notify_device_gone(root_port_hdl);")
