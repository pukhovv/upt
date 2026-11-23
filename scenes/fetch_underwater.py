"""Offline conversion of the pinned Khronos fish and CC0 Poly Haven terrain maps."""
import hashlib
from io import BytesIO
import json
import struct
from urllib.request import urlretrieve


def fetch_underwater(assets):
    from PIL import Image

    path = assets / "BarramundiFish.glb"
    expected = "ecc3bafb6b00f2c8b810863c388e3768a7b7ea0d0335e8cb8c574c266e571f4a"
    if not path.exists():
        urlretrieve("https://raw.githubusercontent.com/KhronosGroup/glTF-Sample-Assets/main/Models/BarramundiFish/glTF-Binary/BarramundiFish.glb",path)
    raw = path.read_bytes()
    if hashlib.sha256(raw).hexdigest() != expected:
        raise ValueError("Unexpected BarramundiFish hash")
    length,kind = struct.unpack_from("<II",raw,12)
    if raw[:4] != b"glTF" or kind != 0x4e4f534a:
        raise ValueError("Expected uncompressed glTF 2.0")
    model = json.loads(raw[20:20+length])
    blob = raw[28+length:]

    def accessor(index):
        a = model["accessors"][index]
        view = model["bufferViews"][a["bufferView"]]
        code = {5126:"f",5123:"H",5125:"I"}[a["componentType"]]
        n = {"SCALAR":1,"VEC2":2,"VEC3":3,"VEC4":4}[a["type"]]
        fmt = "<"+code*n
        stride = view.get("byteStride",struct.calcsize(fmt))
        start = view.get("byteOffset",0)+a.get("byteOffset",0)
        return [struct.unpack_from(fmt,blob,start+i*stride) for i in range(a["count"])]

    primitive = model["meshes"][0]["primitives"][0]
    attr = primitive["attributes"]
    points,normals,uv = (accessor(attr[name]) for name in ["POSITION","NORMAL","TEXCOORD_0"])
    indices = accessor(primitive["indices"])
    # Bake the asset node's 180-degree Y rotation. UVs retain glTF's top-left origin.
    if model["nodes"][0].get("rotation") != [0,1,0,0]:
        raise ValueError("Unexpected fish node transform")
    with (assets/"fish.ply").open("w") as f:
        f.write(f"ply\nformat ascii 1.0\nelement vertex {len(points)}\n")
        for name in ["x","y","z","nx","ny","nz","s","t"]:
            f.write("property float "+name+"\n")
        f.write(f"element face {len(indices)//3}\nproperty list uchar int vertex_indices\nend_header\n")
        for p,n,t in zip(points,normals,uv):
            values = [-p[0],p[1],-p[2],-n[0],n[1],-n[2],t[0],t[1]]
            f.write(" ".join(f"{v:.9g}" for v in values)+"\n")
        for i in range(0,len(indices),3):
            f.write("3 "+" ".join(str(indices[i+j][0]) for j in range(3))+"\n")
    image = model["images"][model["textures"][0]["source"]]
    view = model["bufferViews"][image["bufferView"]]
    png = blob[view.get("byteOffset",0):view.get("byteOffset",0)+view["byteLength"]]
    Image.open(BytesIO(png)).convert("RGB").save(assets/"fish.ppm")
    print("Converted canonical BarramundiFish:",len(points),"vertices,",len(indices)//3,"triangles")

    for name,expected in [
        ("coast_sand_01","58af2e40100f7f405703a86d593af64379f822c4786850def23ecfc921450d54"),
        ("rocky_terrain","429315865fd89c150272592f6d93def174c6f69804a565ff08e53627a885579b"),
    ]:
        path=assets/(name+".jpg")
        if not path.exists():
            url="https://dl.polyhaven.org/file/ph-assets/Textures/jpg/1k/"+name+"/"+name+"_diff_1k.jpg"
            urlretrieve(url,path)
        if hashlib.sha256(path.read_bytes()).hexdigest() != expected:
            raise ValueError("Unexpected texture hash: "+name)
        Image.open(path).convert("RGB").save(assets/(name+".ppm"))
    from build_underwater import build_underwater
    build_underwater(assets)
