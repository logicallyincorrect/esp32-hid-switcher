"""Parametric case; millimetres, +Y toward antenna, USB PCB edge at Y=0.

STLs are placed flat on Z=0. STEP and reference meshes use assembly coordinates.
Run with the dependencies in requirements.txt. See README.md for fit assumptions.
"""
from pathlib import Path
import json
import math
import tempfile

import cadquery as cq
import trimesh

OUT = Path(__file__).resolve().parent
B = json.loads((OUT / "board.json").read_text())
W, WALL, FLOOR = 38.0, 2.0, 2.0
Y0, Y1 = -4.0, B["overall_length"] + 4.0
SEAM, ROOF, FLEX = 22.5, 2.4, 1.2
BUTTON_SPRING = 2.0
TOP = SEAM + ROOF
CLIPS_Y = (23.0, 53.0)
BOARD_PLAY = .15
BOARD_END_PLAY = .05
SNAP_OVERLAP = 2.0


def button_x(name):
    return B[f"{name}_x_from_left"]-B["pcb_width"]/2


def box(x0, x1, y0, y1, z0, z1):
    return cq.Workplane("XY").box(x1-x0, y1-y0, z1-z0).translate(
        ((x0+x1)/2, (y0+y1)/2, (z0+z1)/2))


def cyl(x, y, z, radius, height):
    return cq.Workplane("XY").center(x, y).circle(radius).extrude(height).translate((0, 0, z))


def rounded_box(w, length, height, radius, y, z):
    return (cq.Workplane("XY").box(w, length, height, centered=(True, True, False))
            .edges("|Z").fillet(radius).translate((0, y, z)))


def xz_prism(points, y0, y1):
    # XZ workplane normal is -Y.
    return cq.Workplane("XZ").polyline(points).close().extrude(y1-y0).translate((0, y1, 0))


# Open stroke glyphs: intentional gaps connect letter counters to the lid.
# These are through-cut stencils, not font embossing or shallow engraving.
GLYPHS = {
    "R": [[(-1.2,-2),(-1.2,2)], [(.8,2),(1.2,2),(1.2,0),(-1.2,0)], [(0,0),(1.2,-2)]],
    "D": [[(0,-1.7),(0,1.5)], [(0,-.7),(-1.6,.2),(-1.6,.7)], [(0,-.3),(1.6,.6),(1.6,1)]],
}


def stencil(text, x, y, scale=1, z=SEAM-.1):
    if text == "B":
        # A real rounded B silhouette, with two bridges across the left stem.
        sample=cq.Workplane("XY").text("B",6,1,font="Arial",kind="bold")
        font_size=6*4.8/sample.val().BoundingBox().ylen
        glyph=cq.Workplane("XY").text("B",font_size,TOP+.1-z,font="Arial",kind="bold",combine=True)
        bb=glyph.val().BoundingBox()
        glyph=glyph.translate((x-(bb.xmin+bb.xmax)/2,y-(bb.ymin+bb.ymax)/2,z))
        for dy in (-1.15,1.15):
            glyph=glyph.cut(box(x-4,x+.15,y+dy-.4,y+dy+.4,z-.1,TOP+.2))
        return glyph
    width=.8 if scale==1 else .65
    result=None
    for i,char in enumerate(text):
        cx=x+(i-(len(text)-1)/2)*3.8*scale
        for path in GLYPHS[char]:
            points=[(cx+a*scale,y+b*scale) for a,b in path]
            for a,b in zip(points,points[1:]):
                dx,dy=b[0]-a[0],b[1]-a[1]
                length=math.hypot(dx,dy)
                nx,ny=-dy/length*width/2,dx/length*width/2
                poly=[(a[0]+nx,a[1]+ny),(b[0]+nx,b[1]+ny),
                      (b[0]-nx,b[1]-ny),(a[0]-nx,a[1]-ny)]
                stroke=cq.Workplane("XY").polyline(poly).close().extrude(TOP+.1-z).translate((0,0,z))
                for px,py in (a,b):
                    stroke=stroke.union(cyl(px,py,z,width/2,TOP+.1-z))
                result=stroke if result is None else result.union(stroke)
    return result


def build():
    base = rounded_box(W, Y1-Y0, SEAM, 3, (Y0+Y1)/2, 0)
    base = base.cut(rounded_box(W-2*WALL, Y1-Y0-2*WALL,
                               SEAM+1, 1, (Y0+Y1)/2, FLOOR))
    # Broad common cable opening permits adjacent moulded USB-C plugs.
    base = base.cut(box(-13.8, 13.8, Y0-1, 1, 9.0, SEAM+1))

    pcb_z = B["pcb_bottom_z"]
    pcb_top = pcb_z+B["pcb_thickness"]
    header_bottom = pcb_z-B["header_plastic_height"]
    hold_down_x = 10.15
    for sign in (-1, 1):
        # Two bearing rails per header leave a 1.2 mm pin channel.
        # At least one rail supports each header throughout the XY clearance.
        for offset in (-1.4, 1.4):
            x = sign*B["header_row_x"]+offset
            base = base.union(box(x-.8, x+.8, .5, B["pcb_length"]-.5, FLOOR, header_bottom))
        # Lateral locators, with 0.15 mm PCB clearance each side.
        for y in (5, 30, B["pcb_length"]):
            if sign > 0:
                rib = box(B["pcb_width"]/2+BOARD_PLAY, W/2-WALL+.05, y-1, y+1, FLOOR, pcb_top)
            else:
                rib = box(-W/2+WALL-.05, -B["pcb_width"]/2-BOARD_PLAY, y-1, y+1, FLOOR, pcb_top)
            base = base.union(rib)
        # End fences locate the PCB ends as well as header plastic; no antenna load.
        x = sign*B["header_row_x"]
        for ya, yb in ((-1.75, -BOARD_END_PLAY), (B["pcb_length"]+BOARD_END_PLAY, B["pcb_length"]+1.75)):
            base = base.union(box(x-1.3, x+1.3, ya, yb, FLOOR, pcb_top+.2))
        # Shallow tapered guide ribs locate the Dupont plastic, not the pins.
        # Top ends below PCB so the housings take lateral locating contact.
        housing_outer = B["header_row_x"]+1.27
        for gy in (8, 29, 46):
            xinner = housing_outer+BOARD_PLAY
            pts = [(sign*xinner,FLOOR), (sign*17.05,FLOOR),
                   (sign*17.05,pcb_z-.2), (sign*(xinner+.5),pcb_z-.2),
                   (sign*xinner,pcb_z-.7)]
            base = base.union(xz_prism(pts,gy-1.5,gy+1.5))
        # Through catch windows receive the short teeth; the lid strips flex.
        for y in CLIPS_Y:
            xa,xb=(16.8,20) if sign>0 else (-20,-16.8)
            base=base.cut(box(xa,xb,y-4.5,y+4.5,17.7,20.7))

    # Paired vertical pill vents stay below the catches and clear of top ties.
    # 2 mm spans keep the roofs short; at least 3.5 mm material remains below.
    for sign in (-1,1):
        for vy in (14,20,26,36,42,48):
            vent=(cq.Workplane("YZ").center(vy,6).slot2D(5,2,90)
                  .extrude(2.3).translate((16.9,0,0)))
            if sign<0:
                vent=vent.mirror("YZ",union=False)
            base=base.cut(vent)

    # Stops sit OUTSIDE the PCB insertion envelope. A 45-degree ramp grows
    # inward from each side wall; the widened button paddle lands on its top.
    for sign in (-1, 1):
        stop = [(sign*17.05, 19.9), (sign*17.05, 22.5),
                (sign*14.8, 22.5), (sign*14.8, 22.15)]
        base = base.union(xz_prism(stop, B["button_y"]-1.5, B["button_y"]+1.5))

    lid = rounded_box(W, Y1-Y0, ROOF, 3, (Y0+Y1)/2, SEAM)
    # A lid tongue closes the top of the cable opening; the base needs no long bridge.
    lid = lid.union(box(-13.45, 13.45, Y0, Y0+WALL, 19.2, SEAM+.1))
    # Short locating lips, separated from snap fingers to retain their flex length.
    for sign in (-1, 1):
        xa, xb = (15.5, 16.7) if sign > 0 else (-16.7, -15.5)
        for ya, yb in ((1, 7),):
            lid = lid.union(box(xa, xb, ya, yb, SEAM-2, SEAM+.1))
        # Short solid catches: flex comes from the slotted XY roof strip,
        # rather than bending this 4.5 mm vertical projection.
        for y in CLIPS_Y:
            beam=xz_prism([(15.3,SEAM+.1),(16.9,SEAM+.1),
                           (16.9,18.0),(15.3,18.0)],y-4,y+4)
            hook=xz_prism([(16.85,18.0),(19.0,20.0),
                           (19.0,20.5),(16.85,20.5)],y-4,y+4)
            clip=beam.union(hook)
            if sign<0:
                clip=clip.mirror("YZ",union=False)
            lid=lid.union(clip).clean()
            # Fillet the inside root where the tapered beam meets the lid.
            edges=[]
            for edge in lid.edges().vals():
                c=edge.Center()
                if abs(c.z-SEAM)<.02 and abs(c.x-sign*15.3)<.03 and abs(c.y-y)<.02:
                    edges.append(edge)
            assert edges, "Internal clip root edge missing"
            lid=lid.newObject(edges).fillet(.6)

    # Rear guide stays on rigid roof, clear of the two spring strips.
    lid=lid.union(box(-10,10,57.4,58.6,SEAM-2,SEAM+.1))

    # Four straight independent springs; each joins rigid roof at the
    # centre of its side and carries one unchanged short catch at its free end.
    for sign in (-1,1):
        for rear in (False,True):
            cutout=box(12.8,19.1,18,35.5,SEAM,TOP+.1)
            stem=box(15.2,18.2,19,36,SEAM,TOP)
            head=box(15.2,16.9,19,27,SEAM,TOP)
            rib=box(15.2,16.8,19,36,SEAM-.8,SEAM+.1)
            spring=stem.union(head).union(rib)
            if rear:
                cutout=cutout.mirror("XZ",union=False).translate((0,76,0))
                spring=spring.mirror("XZ",union=False).translate((0,76,0))
            if sign<0:
                cutout=cutout.mirror("YZ",union=False)
                spring=spring.mirror("YZ",union=False)
            lid=lid.cut(cutout).union(spring).clean()
            # Round the plan-view spring root, across the full roof thickness.
            root_y=40.5 if rear else 35.5
            edges=[e for e in lid.edges().vals()
                   if abs(e.Center().x-sign*15.2)<.01
                   and abs(e.Center().y-root_y)<.01
                   and abs(e.Center().z-(SEAM+ROOF/2))<.01]
            assert edges, "Independent spring root missing"
            lid=lid.newObject(edges).fillet(1.2)
    # Trim spring roots to the existing rounded exterior outline.
    lid=lid.intersect(rounded_box(W,Y1-Y0,TOP+1,3,(Y0+Y1)/2,0))

    # Integral U-slotted leaf buttons, thinned from below: top face stays flat.
    button_bottom = TOP-FLEX
    spring_bottom = TOP-BUTTON_SPRING
    peg_tip = pcb_top+B["button_height_above_pcb"]+B["button_rest_gap"]
    for sign in (-1, 1):
        x, y = button_x("reset" if sign < 0 else "boot"), B["button_y"]
        # Compact, stronger springs; closely rounded 0.4 mm perimeter clearance.
        # Local +X is outboard. The root joins rigid roof from the side;
        # the returning inner leg joins the paddle above the unchanged peg.
        outer=sign*15.8
        left,right=min(x-2.8,outer),max(x+2.8,outer)
        # The aperture follows the round paddle and spring bend, instead
        # of surrounding both with an oversized rectangular opening.
        pad_clear=rounded_box(right-left+.8,7.3,ROOF+.1,1.4,y-.25,SEAM).translate(((left+right)/2,0,0))
        stem_clear=box(x-3.2,x+3.2,15.0,23.5,SEAM,TOP+.1)
        cap=cyl(x,23.5,SEAM,3.2,ROOF+.1).intersect(box(x-3.3,x+3.3,23.5,26.8,SEAM,TOP+.1))
        lid=lid.cut(pad_clear.union(stem_clear).union(cap))
        pad=rounded_box(right-left,6.5,FLEX,1.0,y-.25,button_bottom).translate(((left+right)/2,0,0))
        spring=box(-2.8,-.8,y+2.9,23.5,spring_bottom,TOP)
        spring=spring.union(box(.8,2.8,18,23.5,spring_bottom,TOP))
        turn=(cq.Workplane("XY").center(0,23.5).circle(2.8).circle(.8)
              .extrude(BUTTON_SPRING).translate((0,0,spring_bottom)))
        turn=turn.intersect(box(-3,3,23.5,26.5,spring_bottom-.1,TOP+.1))
        spring=spring.union(turn)
        root=rounded_box(2.5,2.4,BUTTON_SPRING,.6,18,spring_bottom).translate((2.45,0,0))
        spring=spring.union(root)
        if sign<0:
            spring=spring.mirror("YZ",union=False)
        lid=lid.union(pad).union(spring.translate((x,0,0)))
        lid = lid.union(cyl(x,y,peg_tip,1.3,button_bottom-peg_tip+.1))
        # Peg root spreads force into the thin pad.
        root = cq.Solid.makeCone(1.3, 2.0, .9, cq.Vector(x, y, button_bottom-.8))
        lid = lid.union(cq.Workplane(obj=root))
        # Board hold-downs sit beside solder joints, with 0.25 mm vertical play.
        for hy in (32, 43):
            hx = sign*hold_down_x
            lid = lid.union(box(hx-.75, hx+.75, hy-1.6, hy+1.6, pcb_top+.25, SEAM+.1))

    # A compact LED viewing aperture between the button tabs.
    lid = lid.cut(box(-3.3, 3.3, 9.8, 11.6, SEAM-.1, TOP+.1))
    # Through-cut labels on moving tabs, clear of peg roots and flexure anchors.
    for name,txt in (("reset","R"),("boot","B")):
        lid=lid.cut(stencil(txt,(-13.5 if name=="reset" else 13.5),B["button_y"]))
    # Freenove C tutorial p28: UART is the right port with USB toward viewer.
    ux=B["usb_left_x_from_left"]-B["pcb_width"]/2
    usb=stencil("D",ux,0,z=19.0)
    usb=usb.union(cyl(ux,-1.7,19,.6,TOP+.1-19))
    usb=usb.union(cyl(ux-1.6,.9,19,.55,TOP+.1-19))
    usb=usb.union(box(ux+1.05,ux+2.15,.65,1.65,19,TOP+.1))
    arrow=cq.Workplane("XY").polyline([(ux-.75,1.35),(ux+.75,1.35),(ux,2.35)]).close().extrude(TOP+.1-19).translate((0,0,19))
    lid=lid.cut(usb.union(arrow))
    px=B["usb_right_x_from_left"]-B["pcb_width"]/2
    bolt=cq.Workplane("XY").polyline([(px+.25,2.3),(px-1.5,-.35),(px-.2,-.35),
                                      (px-.6,-2.3),(px+1.5,.45),(px+.2,.45)]).close().extrude(TOP+.1-19).translate((0,0,19))
    lid=lid.cut(bolt)

    refs = {}
    refs["pcb"] = box(-B["pcb_width"]/2, B["pcb_width"]/2, 0, B["pcb_length"], pcb_z, pcb_top)
    refs["module"] = box(-B["module_width"]/2, B["module_width"]/2,
                         B["overall_length"]-B["module_length"], B["overall_length"],
                         pcb_top, pcb_top+B["module_height"])
    for sign, name in ((-1, "reset"), (1, "boot")):
        x, y = button_x("reset" if sign < 0 else "boot"), B["button_y"]
        refs[name] = box(x-1.8, x+1.8, y-2.5, y+2.5, pcb_top,
                         pcb_top+B["button_height_above_pcb"])
        ux = B["usb_left_x_from_left" if sign < 0 else "usb_right_x_from_left"]-B["pcb_width"]/2
        refs["usb_"+name] = box(ux-B["usb_shell_width"]/2, ux+B["usb_shell_width"]/2,
                                   B["usb_front_y"], B["usb_front_y"]+B["usb_shell_length"],
                                   pcb_top, pcb_top+B["usb_shell_height"])
        hx = sign*B["header_row_x"]
        refs["header_"+name] = box(hx-1.27, hx+1.27, 0, B["pcb_length"], header_bottom, pcb_z)
        pins = None
        for i in range(B["header_count"]):
            py = B["header_first_y"]+i*B["header_pitch"]
            half_pin = B["pin_width"]/2
            pin = box(hx-half_pin, hx+half_pin, py-half_pin, py+half_pin,
                      pcb_z-B["pin_length_below_pcb"], pcb_top+.7)
            pins = pin if pins is None else pins.union(pin)
        refs["pins_"+name] = pins
    return base.clean(), lid.clean(), refs


def overlap(a, b):
    common = a.intersect(b)
    return sum(s.Volume() for s in common.solids().vals())


def export_and_validate(base, lid, refs):
    report = {"status": "CAD checked; physical fit, slicing and loads untested",
              "revision": "Heavy straight snap springs with underside ribs for infrequent opening; unchanged base and buttons", "board": B, "parts": {}, "clearances_mm": {
                  "pin_to_floor": B["pcb_bottom_z"]-B["pin_length_below_pcb"]-FLOOR,
                  "button_rest": B["button_rest_gap"], "snap_engagement": SNAP_OVERLAP,
                  "board_x_play_each_side": BOARD_PLAY, "board_y_play_each_end":BOARD_END_PLAY,
                  "catch_vertical_play": .2,
                  "button_stop_at_base_ramp": TOP-FLEX-22.5,
              }}
    # Stop pads are supported by the base ramps at 1.2 mm travel. Probe just
    # before/after contact without pretending to simulate the printed flexure.
    stop_checks = {}
    for sign in (-1,1):
        x0,x1 = (14.9,15.7) if sign>0 else (-15.7,-14.9)
        pad = lid.intersect(box(x0,x1,B["button_y"]-1,B["button_y"]+1,TOP-FLEX,TOP))
        before = overlap(base,pad.translate((0,0,-1.19)))
        after = overlap(base,pad.translate((0,0,-1.21)))
        assert before < 1e-5 and after > .001, (before,after)
        stop_checks[str(sign)] = {"at_1_19_mm":before,"at_1_21_mm":after}
    report["button_stop_contact_mm3"] = stop_checks
    report["feature_dimensions_mm"] = {"bearing_rail_width":1.6,
        "hold_down_section":[1.5,3.2],"snap_count":4,"snap_width":8.0,"snap_projection":4.5,
        "roof_spring_count":4,"roof_spring_leg_width":3.0, "spring_underside_rib_width":1.6, "spring_underside_rib_height":.8,
        "spring_shape":"straight","spring_free_length":16.5,
        "spring_plan_root_radius":1.2,"spring_envelope_length":17.5,
        "spring_inward_relief":2.4,
        "snap_root_thickness":1.6,"snap_tip_thickness":1.6,"snap_root_fillet_radius":.6,
        "release_window_width":9,"release_window_height":3,
        "vent_count":12,"vent_width":2,"vent_height":5,
        "R_and_USB_stroke":.8,"B_stencil_bridge":.8,
        "button_spring_count":2,"button_spring_thickness":BUTTON_SPRING,"button_paddle_thickness":FLEX,"button_spring_leg_width":2.0,"button_perimeter_clearance":.4,
        "button_return_centre_y":23.5,
        "button_return_outer_radius":2.8,"button_return_inner_radius":.8,
        "peg_diameter":2.6,"button_stop_ramp_degrees":45}
    # Preserve measured board and button clearances.
    assert report["clearances_mm"]["pin_to_floor"] >= .4
    assembled = cq.Assembly(name="Freenove_case")
    assembled.add(base, name="base", color=cq.Color(.12,.30,.34))
    assembled.add(lid, name="lid", color=cq.Color(.70,.83,.82))
    staging = tempfile.TemporaryDirectory(prefix="freenove-case-")
    pending = Path(staging.name)
    for name, original, printable in (("base", base, base),
                                      ("lid", lid, lid.rotate((0,0,0), (1,0,0), 180).translate((0,0,TOP)))):
        assert original.val().isValid(), name
        assert original.solids().size() == 1, (name, original.solids().size())
        target = pending / f"{name}.stl"
        cq.exporters.export(printable, str(target), tolerance=.04, angularTolerance=.12)
        mesh = trimesh.load_mesh(target)
        assert mesh.is_watertight and mesh.is_winding_consistent, name
        assert len(mesh.split()) == 1, name
        assert abs(mesh.bounds[0,2]) < 1e-5
        report["parts"][name] = {"valid_brep": True, "solids": 1, "watertight": True,
                                 "bounds_mm": mesh.extents.tolist(), "volume_mm3": mesh.volume}
    interference = {"base_vs_lid": overlap(base, lid)}
    for name, ref in refs.items():
        for part_name, part in (("base", base), ("lid", lid)):
            interference[f"{part_name}_vs_{name}"] = overlap(part, ref)
    assert max(interference.values()) < 1e-5, interference
    report["interference_mm3"] = interference
    # Verify actual retaining shoulders catch when the lid is lifted, and the
    # short hook clears the wall after inward release. This is geometry only;
    # it does not simulate spring strain, printed strength or return force.
    latch_checks = {}
    for sign in (-1,1):
        for y in CLIPS_Y:
            shoulder=box(16.85,19.0,y-4,y+4,20.0,20.5)
            released=shoulder.translate((-2.2,0,0))
            if sign<0:
                shoulder=shoulder.mirror("YZ",union=False)
                released=released.mirror("YZ",union=False)
            seated=overlap(base,shoulder)
            caught=overlap(base,shoulder.translate((0,0,.35)))
            freed=max(overlap(base,released.translate((0,0,dz))) for dz in (0,.35,2,5,10))
            tolerance_catch=overlap(base,shoulder.translate((-sign*.4,0,.35)))
            assert seated<1e-5 and caught>.1 and freed<1e-5 and tolerance_catch>.1
            latch_checks[f"{sign},{y}"]={"seated":seated,"lift_0_35mm":caught,
                "inward_release_2_2mm_swept":freed, "catch_after_0_4mm_engagement_loss":tolerance_catch}
    report["latch_retention_interference_mm3"] = latch_checks
    # Topology check: sever only the four intended root necks in a temporary
    # copy. Exactly four separate catch/spring pieces must detach from roof.
    detached=lid
    for sign in (-1,1):
        xa,xb=(13.4,19.2) if sign>0 else (-19.2,-13.4)
        for ya,yb in ((34.3,34.6),(41.4,41.7)):
            detached=detached.cut(box(xa,xb,ya,yb,SEAM-1,TOP+.1))
    pieces=detached.solids().vals()
    assert len(pieces)==5, ("Independent spring topology",len(pieces))
    tooth_owners=[]
    for sign in (-1,1):
        for y in CLIPS_Y:
            probe=box(16.9,18.4,y-3.8,y+3.8,20.05,20.45)
            if sign<0:
                probe=probe.mirror("YZ",union=False)
            owners=[i for i,part in enumerate(pieces)
                    if overlap(cq.Workplane(obj=part),probe)>1]
            assert len(owners)==1, owners
            tooth_owners.extend(owners)
    assert len(set(tooth_owners))==4, tooth_owners
    report["independent_spring_topology"]={"severed_roots":4,
        "resulting_solids":5,"distinct_spring_with_one_tooth_each":4,
        "physical_deflection_simulated":False}

    # Check each new button is attached only through its own side root.
    detached_buttons=lid
    for sign,name in ((-1,"reset"),(1,"boot")):
        x=button_x(name)
        xa,xb=(x+3.0,x+3.5) if sign>0 else (x-3.5,x-3.0)
        detached_buttons=detached_buttons.cut(box(xa,xb,16.7,19.3,TOP-BUTTON_SPRING-.1,TOP+.1))
    button_pieces=detached_buttons.solids().vals()
    assert len(button_pieces)==3, ("Button root topology",len(button_pieces))
    owners=[]
    for name in ("reset","boot"):
        witness=cyl(button_x(name),B["button_y"],16,.7,1)
        hits=[i for i,part in enumerate(button_pieces)
              if overlap(cq.Workplane(obj=part),witness)>1]
        assert len(hits)==1, hits
        owners.extend(hits)
    assert len(set(owners))==2
    report["independent_button_topology"]={"severed_roots":2,
        "resulting_solids":3,"one_peg_per_button":True,"physical_deflection_simulated":False}

    # Conservative continuous insertion sweeps: every board envelope extends upward.
    drop_in = {}
    for name, ref in refs.items():
        bb = ref.val().BoundingBox()
        sweep = box(bb.xmin, bb.xmax, bb.ymin, bb.ymax, bb.zmin, bb.zmax+40)
        drop_in[name] = overlap(base, sweep)
    assert max(drop_in.values()) < 1e-5, drop_in
    report["board_vertical_insertion_interference_mm3"] = drop_in
    # Actual permitted lateral play must not put pins onto bearing rails.
    lateral_play = {}
    for dx in (-.14, 0, .14):
        for dy in (-.04, 0, .04):
            total = 0
            for name, ref in refs.items():
                moved = ref.translate((dx, dy, 0))
                total += overlap(base, moved)+overlap(lid, moved)
            lateral_play[f"{dx},{dy}"] = total
    assert max(lateral_play.values()) < 1e-5, lateral_play
    report["board_xy_play_interference_mm3"] = lateral_play
    report["zip_tie_slots_removed"] = True
    report["not_validated"] = ["Unmeasured USB shell depth and module footprint", "Printed tolerances",
        "Slicer toolpaths/bridging", "Snap and actuator fatigue/force", "RF performance"]
    assembled.export(str(pending / "case.step"))
    # Reference board intentionally kept separate from printable exports.
    reference = cq.Assembly(name="approximate_board_reference")
    for name, obj in refs.items():
        reference.add(obj, name=name)
    reference.export(str(pending / "board-reference.step"))
    coupon_checks={}
    crop=box(11.0,19.1,16.6,Y1+.1,0,TOP+.1)
    for name,part in (("snap-test-base",base),("snap-test-lid",lid)):
        coupon=part.intersect(crop).clean()
        if name.endswith("lid"):
            coupon=coupon.rotate((0,0,0),(1,0,0),180).translate((0,0,TOP))
        assert coupon.val().isValid() and coupon.solids().size()==1, (name,coupon.solids().size())
        target=pending/f"{name}.stl"
        cq.exporters.export(coupon,str(target),tolerance=.04,angularTolerance=.12)
        mesh=trimesh.load_mesh(target)
        assert mesh.is_watertight and mesh.is_winding_consistent and len(mesh.split())==1
        assert abs(mesh.bounds[0,2])<1e-5
        coupon_checks[name]={"watertight":True,"single_solid":True,"volume_mm3":mesh.volume}
    report["snap_test_coupons"]=coupon_checks
    (pending / "validation.json").write_text(json.dumps(report, indent=2)+"\n")
    for artifact in pending.iterdir():
        artifact.replace(OUT / artifact.name)
    staging.cleanup()
    return report


if __name__ == "__main__":
    base, lid, refs = build()
    report = export_and_validate(base, lid, refs)
    print(json.dumps({"parts": report["parts"], "clearances_mm": report["clearances_mm"]}, indent=2))
