"""Depth-buffered rendering of actual CAD solids, including the rear tie slots."""
from pathlib import Path
import math
import tempfile

# Load OpenCascade before VTK to avoid macOS native-library initialization conflicts.
import cadquery as cq

import matplotlib
matplotlib.use('Agg')
import matplotlib.pyplot as plt
from matplotlib.colors import to_rgb
import numpy as np
import vtk
from vtk.util.numpy_support import numpy_to_vtk, vtk_to_numpy


def panel(objects, target, distance=110, elev=32, azim=-62, scale=43):
    renderer = vtk.vtkRenderer()
    renderer.SetBackground(*to_rgb('#f5f7f8'))
    for obj, color, shift in objects:
        # Let OpenCascade export the mesh in C++; avoid per-triangle Python/OCP
        # calls, which are unusually slow on this macOS CAD runtime.
        with tempfile.TemporaryDirectory(prefix="case-preview-") as tmp:
            mesh_path = str(Path(tmp)/"part.stl")
            cq.exporters.export(obj, mesh_path, tolerance=.05, angularTolerance=.12)
            reader = vtk.vtkSTLReader(); reader.SetFileName(mesh_path); reader.Update()
            poly = vtk.vtkPolyData(); poly.DeepCopy(reader.GetOutput())
        transform = vtk.vtkTransform(); transform.Translate(*shift)
        moved = vtk.vtkTransformPolyDataFilter(); moved.SetInputData(poly)
        moved.SetTransform(transform); moved.Update(); poly = moved.GetOutput()
        normals = vtk.vtkPolyDataNormals(); normals.SetInputData(poly)
        normals.SetFeatureAngle(35); normals.ConsistencyOn(); normals.AutoOrientNormalsOn()
        mapper = vtk.vtkPolyDataMapper(); mapper.SetInputConnection(normals.GetOutputPort())
        actor = vtk.vtkActor(); actor.SetMapper(mapper)
        actor.GetProperty().SetColor(*to_rgb(color))
        actor.GetProperty().SetAmbient(.25); actor.GetProperty().SetDiffuse(.75)
        actor.GetProperty().SetSpecular(.12); actor.GetProperty().SetSpecularPower(25)
        renderer.AddActor(actor)
    camera = renderer.GetActiveCamera()
    e,a=math.radians(elev),math.radians(azim)
    delta=np.asarray([math.cos(e)*math.cos(a),math.cos(e)*math.sin(a),math.sin(e)])*distance
    camera.SetPosition(*(np.asarray(target)+delta)); camera.SetFocalPoint(*target)
    camera.SetViewUp(0,0,1); camera.ParallelProjectionOn(); camera.SetParallelScale(scale)
    renderer.ResetCameraClippingRange()
    window=vtk.vtkRenderWindow(); window.SetOffScreenRendering(1)
    window.SetSize(1100,850); window.SetMultiSamples(8); window.AddRenderer(renderer)
    window.Render()
    grab=vtk.vtkWindowToImageFilter(); grab.SetInput(window); grab.ReadFrontBufferOff(); grab.Update()
    output=grab.GetOutput(); width,height,_=output.GetDimensions()
    result=vtk_to_numpy(output.GetPointData().GetScalars()).reshape(height,width,3)[::-1].copy()
    window.Finalize()
    return result


def render(base,lid,refs,out,cfg):
    bc,lc='#277b82','#c6dcdd'
    objects=[(base,bc,(0,0,0)),(lid,lc,(0,0,0))]
    images=[panel(objects,(0,28,12),scale=35)]
    exploded=[(base,bc,(0,0,0)),(lid,lc,(0,0,46))]
    for name,ref in refs.items():
        color=('#31554a' if name=='pcb' else '#a5afb4' if name=='module' else
               '#c0a15b' if name.startswith('pins') else '#a9b5bc' if name.startswith('usb') else '#34464c')
        exploded.append((ref,color,(0,0,22)))
    images.append(panel(exploded,(0,28,35),elev=20,scale=45))
    flipped=lid.rotate((0,0,0),(1,0,0),180).translate((0,cfg['overall_length'],24.9))
    images.append(panel([(flipped,lc,(0,0,0))],(0,28,7),elev=48,scale=35))
    images.append(panel([(lid,lc,(0,0,0))],(0,28,23),elev=89,azim=-90,scale=35))
    titles=['Assembled · side vents and release windows',
            'Exploded · approximate board reference',
            'Lid underside · four independent straight snap springs',
            'Top view · curved R / B buttons · USB and power icons']
    fig,axes=plt.subplots(2,2,figsize=(14,11),facecolor='#f5f7f8')
    for ax,im,title in zip(axes.flat,images,titles):
        ax.imshow(im);ax.axis('off');ax.set_title(title,loc='left',fontsize=13,fontweight='bold',color='#233c46')
    fig.text(.04,.96,'Freenove ESP32-S3 · snap-fit case',fontsize=23,fontweight='bold',color='#233c46')
    fig.text(.04,.929,'Reinforced snap tabs • side ventilation • through-cut labels',fontsize=12,color='#526970')
    fig.text(.04,.025,'Prototype: geometry checked; printed fit, button feel and latch strength need a physical test.',fontsize=11,color='#526970')
    fig.subplots_adjust(left=.035,right=.97,top=.88,bottom=.065,wspace=.09,hspace=.14)
    fig.savefig(Path(out)/'assembled-preview.png',dpi=180,facecolor=fig.get_facecolor());plt.close(fig)
    # Thin section through one real latch makes the shoulder and fillet visible.
    section=cq.Workplane("XY").box(6, .6, 15).translate((16.5,53,21))
    detail=panel([(base.intersect(section),bc,(0,0,0)),
                  (lid.intersect(section),lc,(0,0,0))],
                 (16.5,53,21),elev=0,azim=-90,scale=8)
    plt.imsave(Path(out)/'latch-section.png',detail)
    plt.imsave(Path(out)/'snap-root-closeup.png',detail)



if __name__=='__main__':
    from generate import build,B,OUT
    render(*build(),OUT,B)
