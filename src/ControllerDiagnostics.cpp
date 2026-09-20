#include "ControllerDiagnostics.h"
#include "ControllerTracker.h"
#include <Arduino.h>
extern "C" {
#include <nimble/nimble/host/src/ble_hs_priv.h>
#include <nimble/nimble/host/src/ble_hs_conn_priv.h>
int __real_ble_hs_tx_data(struct os_mbuf *om);
int __real_ble_hs_hci_evt_process(struct ble_hci_ev *ev);
}
static ControllerTracker tracker;
static portMUX_TYPE trackerMux=portMUX_INITIALIZER_UNLOCKED;
static uint16_t keyboardAttribute=0xffff,mouseAttribute=0xffff,relativeMouseAttribute=0xffff;
void ControllerDiagnostics::reports(uint16_t keyboard,uint16_t mouse,uint16_t relativeMouse){portENTER_CRITICAL(&trackerMux);keyboardAttribute=keyboard;mouseAttribute=mouse;relativeMouseAttribute=relativeMouse;portEXIT_CRITICAL(&trackerMux);}
extern "C" int __wrap_ble_hs_tx_data(struct os_mbuf *om){
  uint8_t header[11]{};const unsigned length=OS_MBUF_PKTLEN(om);
  const bool valid=length>=4&&os_mbuf_copydata(om,0,length<11?length:11,header)==0;
  const uint16_t handle=(header[0]|uint16_t(header[1])<<8)&0x0fff;
  uint8_t kind=0;
  // Single-fragment ATT notification: HCI(4), L2CAP(4), opcode and attr(3).
  const uint16_t payload=header[2]|uint16_t(header[3])<<8;
  const uint16_t l2length=header[4]|uint16_t(header[5])<<8;
  if(valid&&length>=11&&(header[1]&0x30)!=0x10&&header[6]==4&&header[7]==0&&header[8]==0x1b&&payload==l2length+4){
    const uint16_t attr=header[9]|uint16_t(header[10])<<8;
    portENTER_CRITICAL(&trackerMux);kind=(attr==mouseAttribute||attr==relativeMouseAttribute)?2:attr==keyboardAttribute?1:0;portEXIT_CRITICAL(&trackerMux);
  }
  const uint32_t submitted=micros();
  const int rc=__real_ble_hs_tx_data(om);
  // NimBLE holds its host lock across this call and outstanding accounting.
  if(rc==0&&valid){portENTER_CRITICAL(&trackerMux);tracker.submit(handle,kind,submitted);portEXIT_CRITICAL(&trackerMux);}
  return rc;
}
extern "C" int __wrap_ble_hs_hci_evt_process(struct ble_hci_ev *ev){
  const uint8_t *raw=reinterpret_cast<const uint8_t *>(ev);
  const uint8_t code=raw[0],len=raw[1];
  uint16_t handles[63]{},counts[63]{};unsigned n=0;uint16_t disconnected=0xffff;
  if(code==0x13&&len>=1&&raw[2]<=63&&len==1+raw[2]*4){n=raw[2];for(unsigned i=0;i<n;++i){const auto p=raw+3+i*4;handles[i]=p[0]|uint16_t(p[1])<<8;counts[i]=p[2]|uint16_t(p[3])<<8;}}
  if(code==0x05&&len==4&&raw[2]==0)disconnected=raw[3]|uint16_t(raw[4])<<8;
  const int rc=__real_ble_hs_hci_evt_process(ev);
  if(rc==0){const uint32_t now=micros();portENTER_CRITICAL(&trackerMux);for(unsigned i=0;i<n;++i)tracker.complete(handles[i],counts[i],now);if(disconnected!=0xffff)tracker.disconnect(disconnected);portEXIT_CRITICAL(&trackerMux);}
  return rc;
}
void ControllerDiagnostics::appendStatus(JsonObject out){
  // Static snapshot avoids putting several KB on the Arduino loop stack.
  static ControllerTracker snapshot;
  portENTER_CRITICAL(&trackerMux);snapshot=tracker;portEXIT_CRITICAL(&trackerMux);
  out["submitted_acl"]=snapshot.sent;out["completed_acl"]=snapshot.returned;
  out["unmatched"]=snapshot.unmatched;out["tracking_overflow"]=snapshot.overflow;out["discarded_on_disconnect"]=snapshot.discarded;
  const char *names[]={"other_completion","keyboard_completion","mouse_completion"};
  for(unsigned i=0;i<3;++i){const auto &s=snapshot.completed[i];auto stats=out[names[i]].to<JsonObject>();stats["samples"]=s.count;stats["mean_ms"]=s.mean()/1000.;stats["max_ms"]=s.max/1000.;stats["p95_upper_ms"]=s.count?s.p95Upper()/1000.:0;}
  auto links=out["links"].to<JsonArray>();
  for(const auto &l:snapshot.links)if(l.handle!=0xffff){
    uint32_t queued=0,outstanding=0,attQueued=0;bool found=false;
    ble_hs_lock();auto conn=ble_hs_conn_find(l.handle);if(conn){found=true;outstanding=conn->bhc_outstanding_pkts;os_mbuf_pkthdr *p;STAILQ_FOREACH(p,&conn->bhc_tx_q,omp_next)++queued;STAILQ_FOREACH(p,&conn->att_tx_q,omp_next)++attQueued;}ble_hs_unlock();
    auto link=links.add<JsonObject>();link["handle"]=l.handle;link["connected"]=found;link["pending_tracked"]=l.count;link["peak_pending"]=l.peak;link["tracking_valid"]=!l.desynced;
    link["completion_events"]=l.batchSizes.count;link["mean_packets_per_event"]=l.batchSizes.mean();link["max_packets_per_event"]=l.batchSizes.max;
    link["completion_spacing_mean_ms"]=l.creditSpacing.samples.mean()/1000.;link["completion_spacing_max_ms"]=l.creditSpacing.samples.max/1000.;link["completion_idle_gaps"]=l.creditSpacing.idleGaps;
    link["oldest_pending_ms"]=l.count?(micros()-l.queue[l.head].time)/1000.:0;link["controller_outstanding"]=outstanding;link["host_queued"]=queued;link["att_queued"]=attQueued;
  }
}

void ControllerDiagnostics::printStatus(){
  TimingStats mouse,keyboard,window,batch,spacing;uint32_t sent,returned,unmatched,overflow,pending=0,peak=0;
  portENTER_CRITICAL(&trackerMux);mouse=tracker.completed[2];keyboard=tracker.completed[1];sent=tracker.sent;returned=tracker.returned;unmatched=tracker.unmatched;overflow=tracker.overflow;
  window=tracker.windowMouse;batch=tracker.windowBatch;spacing=tracker.windowSpacing;tracker.windowMouse={};tracker.windowBatch={};tracker.windowSpacing={};
  for(const auto &l:tracker.links){pending+=l.count;if(l.peak>peak)peak=l.peak;}portEXIT_CRITICAL(&trackerMux);
  Serial.printf("[Controller window] mouse_n=%lu mean_ms=%.2f max_ms=%.2f completion_events=%lu packets_per_event=%.2f max_batch=%lu event_spacing_ms=%.2f\n",(unsigned long)window.count,window.mean()/1000.,window.max/1000.,(unsigned long)batch.count,batch.mean(),(unsigned long)batch.max,spacing.mean()/1000.);
  Serial.printf("[Controller] sent=%lu returned=%lu pending=%lu peak_per_link=%lu unmatched=%lu overflow=%lu mouse_n=%lu mouse_mean_ms=%.2f mouse_max_ms=%.2f keyboard_n=%lu keyboard_mean_ms=%.2f keyboard_max_ms=%.2f\n",(unsigned long)sent,(unsigned long)returned,(unsigned long)pending,(unsigned long)peak,(unsigned long)unmatched,(unsigned long)overflow,(unsigned long)mouse.count,mouse.mean()/1000.,mouse.max/1000.,(unsigned long)keyboard.count,keyboard.mean()/1000.,keyboard.max/1000.);
}
