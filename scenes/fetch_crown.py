"""Import Martin Lubich's Imperial Crown from the pinned PBRT-v4 scene."""
from concurrent.futures import ThreadPoolExecutor
import hashlib
import json
import math
import re
import shlex
import struct
from urllib.request import urlopen

REVISION = "30cf4a0346ae5a80a2d7a530a3ef7d0fa4f70572"
BASE = f"https://raw.githubusercontent.com/mmp/pbrt-v4-scenes/{REVISION}/crown/"


def download(path, sha, target):
    def digest(raw):
        return hashlib.sha1(b"blob " + str(len(raw)).encode() + b"\0" + raw).hexdigest()

    if not target.exists() or digest(target.read_bytes()) != sha:
        target.parent.mkdir(parents=True, exist_ok=True)
        with urlopen(BASE + path, timeout=60) as response:
            raw = response.read()
        if digest(raw) != sha:
            raise ValueError("Unexpected Crown asset: " + path)
        target.write_bytes(raw)


def fetch_crown(assets):
    root = assets / "crown"
    root.mkdir(exist_ok=True)
    download("crown.pbrt", "9a22c7f33b0f009b795eb7adb8d2052e915ad65e", root / "source.pbrt")
    source = (root / "source.pbrt").read_text()
    needed = set(re.findall(r'"((?:geometry|textures)/[^\"]+)"', source))
    entries = []
    for folder, tree in [("geometry", "e568787509de5268f044848e4c786b3724a84876"),
                         ("textures", "c8578b904e4634a8bec3df5641d542e39d40e860")]:
        manifest = root / (folder + ".json")
        if not manifest.exists():
            url = f"https://api.github.com/repos/mmp/pbrt-v4-scenes/git/trees/{tree}?recursive=1"
            with urlopen(url, timeout=60) as response:
                manifest.write_bytes(response.read())
        for entry in json.loads(manifest.read_text())["tree"]:
            path = folder + "/" + entry["path"]
            if path in needed:
                entries.append((path, entry["sha"], root / "source" / path))
    if {e[0] for e in entries} != needed:
        raise ValueError("Missing Crown assets in pinned tree")
    print(f"Fetching Crown: {len(entries)} assets, "
          f"{sum(p.stat().st_size for p in root.rglob('*.ply')) / 1e6:.1f} MB cached", flush=True)
    with ThreadPoolExecutor(max_workers=12) as pool:
        list(pool.map(lambda e: download(*e), entries))
    convert_crown(source, root)


def convert_crown(source, root):
    from PIL import Image

    def params(tokens, start=2):
        result = {}
        while start < len(tokens):
            key = tokens[start]
            start += 1
            if tokens[start] == "[":
                end = tokens.index("]", start)
                result[key] = tokens[start + 1:end]
                start = end + 1
            else:
                result[key] = [tokens[start]]
                start += 1
        return result

    def scalar(p, name, default):
        return float(p.get(name, [default])[0])

    def rgb(values):
        return "[" + " ".join(f"{v:.8g}" for v in values) + "]"

    def linear(v):
        return v / 12.92 if v <= .04045 else ((v + .055) / 1.055) ** 2.4

    def encoded(v):
        v = min(1, max(0, v))
        return round(255 * (12.92 * v if v <= .0031308 else 1.055 * v ** (1 / 2.4) - .055))

    textures, materials, media, groups = {}, {}, {}, {}
    declarations, world, stack = [], [], []
    material, medium = None, ""
    gold = [1, .766, .336]

    def image_texture(name):
        kind, p = textures[name]
        if kind == "scale":
            image, scale = image_texture(p["texture tex"][0])
            return image, scale * scalar(p, "float scale", 1)
        if kind != "imagemap":
            raise ValueError("Unsupported Crown texture: " + kind)
        image = Image.open(root / "source" / p["string filename"][0]).convert("RGB")
        return image.transpose(Image.Transpose.FLIP_TOP_BOTTOM), 1

    def save_texture(name, image):
        filename = "texture-" + name + ".ppm"
        image.save(root / filename)
        declarations.append(f'Texture "{name}" "spectrum" "imagemap" '
                            f'"string filename" "assets/crown/{filename}"')

    def material_line(name, absorption):
        p = materials[name]
        kind = p["string type"][0]
        if kind == "mix":
            # Bake the two enamel/metal masks into RGB reflectance for UPT's conductor.
            a, b = (materials[n] for n in p["string materials"])
            colors = [list(map(float, q.get("rgb reflectance", gold))) for q in (a, b)]
            mask, scale = image_texture(p["texture amount"][0])
            channel = mask.getchannel("R")
            channels = [channel.point([encoded((1 - linear(i / 255) * scale) * colors[0][c] +
                                               linear(i / 255) * scale * colors[1][c])
                                       for i in range(256)]) for c in range(3)]
            save_texture(name, Image.merge("RGB", channels))
            return f'Material "conductor" "texture reflectance" "{name}" "float roughness" .04'
        if kind == "dielectric":
            eta = scalar(p, "float eta", 0)
            if not eta:
                lo, a, hi, b = map(float, p["spectrum eta"])
                eta = a + (b - a) * (550 - lo) / (hi - lo)
            return (f'Material "dielectric" "float eta" {eta:.8g} '
                    f'"rgb transmittance" {rgb(math.exp(-s) for s in absorption)}')
        if kind not in ("conductor", "coateddiffuse", "diffuse"):
            raise ValueError("Unsupported Crown material: " + kind)
        line = f'Material "{kind}"'
        if "texture reflectance" in p:
            line += ' "texture reflectance" "' + p["texture reflectance"][0] + '"'
        else:
            color = map(float, p.get("rgb reflectance", gold if kind == "conductor" else [.5]*3))
            line += ' "rgb reflectance" ' + rgb(color)
        if kind == "conductor":
            # PBRT maps roughness to sqrt(r), whereas UPT uses r*r.
            if "texture roughness" in p:
                image, scale = image_texture(p["texture roughness"][0])
                table = [encoded((linear(i / 255) * scale) ** .25) for i in range(256)]
                name = "upt-roughness-" + name
                save_texture(name, image.point(table * 3))
                line += f' "texture roughness" "{name}"'
            else:
                line += f' "float roughness" {scalar(p, "float roughness", 0) ** .25:.8g}'
        if "texture displacement" in p:
            line += ' "texture displacement" "' + p["texture displacement"][0] + '"'
        return line

    source = re.sub(r"(?m)#.*$", "", source)
    blocks = re.split(r"(?m)(?=^[ \t]*[A-Z][A-Za-z]*\b)", source)
    for block in blocks:
        lexer = shlex.shlex(block, posix=True, punctuation_chars="[]")
        lexer.whitespace_split = True
        t = list(lexer)
        if not t:
            continue
        command = t[0]
        if command == "Texture":
            p = params(t, 4)
            textures[t[1]] = (t[3], p)
            if t[3] == "imagemap":
                image, _ = image_texture(t[1])
                save_texture(t[1], image)
            elif t[3] == "scale":
                declarations.append(f'Texture "{t[1]}" "spectrum" "scale" '
                                    f'"texture tex1" "{p["texture tex"][0]}" '
                                    f'"float tex2" {scalar(p, "float scale", 1):.8g}')
            else:
                raise ValueError("Unsupported Crown texture: " + t[3])
        elif command == "MakeNamedMaterial":
            materials[t[1]] = params(t)
        elif command == "MakeNamedMedium":
            media[t[1]] = tuple(map(float, params(t).get("rgb sigma_a", [0, 0, 0])))
        elif command == "AttributeBegin":
            stack.append((material, medium))
        elif command == "AttributeEnd":
            material, medium = stack.pop()
        elif command == "NamedMaterial":
            material = t[1]
        elif command == "MediumInterface":
            medium = t[1] or t[2]
        elif command == "Shape" and t[1] == "plymesh":
            key = (material, media.get(medium, (0, 0, 0)))
            groups.setdefault(key, []).append(root / "source" / params(t)["string filename"][0])
        elif command == "AreaLightSource":
            world.append('AreaLightSource "diffuse" "rgb L" [5 4.55 4.05]')
        elif command == "Shape":
            p = params(t)
            if t[1] == "sphere":
                world.append('Material "diffuse" "rgb reflectance" [.2 .2 .2]')
                world.append('Shape "sphere" "float radius" ' + p["float radius"][0])
            else:
                points = list(map(float, p["point3 P"]))
                center_x = sum(points[::3]) / (len(points) // 3)
                # Keep the side softboxes outside the widescreen camera view.
                if abs(center_x) > 30:
                    for j in range(0, len(points), 3):
                        points[j] += .25 * center_x
                world.append('Shape "trianglemesh" "integer indices" ' + rgb(map(int, p["integer indices"])) +
                             ' "point3 P" ' + rgb(points))
    triangles = 0
    for i, ((name, absorption), meshes) in enumerate(groups.items()):
        filename = f"mesh-{i}.ply"
        glass = materials[name]["string type"] == ["dielectric"]
        triangles += merge_meshes(meshes, root / filename, glass)
        world += [material_line(name, absorption),
                  f'Shape "plymesh" "string filename" "assets/crown/{filename}"']
    scene = ["# Imperial Crown by Martin Lubich; PBRT-v4 geometry and RGB material adaptation.",
             "# Fetch assets with: python3 scenes/fetch.py crown",
             "LookAt 0 5.5 24  0 9 -10  0 1 0",
             'Camera "perspective" "float fov" 56',
             "WorldBegin", "Scale -1 1 1"]
    (root.parent.parent / "crown.pbrt").write_text("\n".join(scene + declarations + world) + "\n")
    print(f"Converted Crown: {triangles:,} triangles, {len(groups)} material groups", flush=True)


def merge_meshes(paths, target, glass):
    meshes = []
    for path in paths:
        with path.open("rb") as f:
            header = []
            while (line := f.readline()) != b"end_header\n":
                if not line:
                    raise ValueError("Missing PLY header: " + str(path))
                header.append(line.decode().strip())
            nv = int(next(l for l in header if l.startswith("element vertex ")).split()[-1])
            nt = int(next(l for l in header if l.startswith("element face ")).split()[-1])
            properties = [l.split()[-1] for l in header if l.startswith("property float ")]
            if "format binary_little_endian 1.0" not in header or properties not in (
                    ["x", "y", "z", "nx", "ny", "nz"],
                    ["x", "y", "z", "nx", "ny", "nz", "u", "v"]):
                raise ValueError("Unexpected Crown PLY layout: " + str(path))
            raw = f.read()
        split = nv * len(properties) * 4
        vertices = list(struct.iter_unpack("<" + "f" * len(properties), raw[:split]))
        faces = list(struct.iter_unpack("<Biii", raw[split:]))
        if len(faces) != nt or any(n != 3 or min(a, b, c) < 0 or max(a, b, c) >= nv
                                   for n, a, b, c in faces):
            raise ValueError("Expected Crown triangles: " + str(path))
        volume = 0
        if glass:
            for _, a, b, c in faces:
                x, y, z = (vertices[j][:3] for j in (a, b, c))
                volume += (x[0] * (y[1]*z[2] - y[2]*z[1]) +
                           x[1] * (y[2]*z[0] - y[0]*z[2]) + x[2] * (y[0]*z[1] - y[1]*z[0]))
        meshes.append((vertices, faces, volume < 0))
    nv = sum(len(v) for v, _, _ in meshes)
    nt = sum(len(f) for _, f, _ in meshes)
    with target.open("w") as f:
        f.write(f"ply\nformat ascii 1.0\nelement vertex {nv}\n")
        for name in ["x", "y", "z", "nx", "ny", "nz", "u", "v"]:
            f.write("property float " + name + "\n")
        f.write(f"element face {nt}\nproperty list uchar int vertex_indices\nend_header\n")
        for vertices, _, flip in meshes:
            for v in vertices:
                normal = tuple(-n for n in v[3:6]) if flip else v[3:6]
                f.write(" ".join(f"{x:.9g}" for x in (*v[:3], *normal, *(v[6:] or (0, 0)))) + "\n")
        offset = 0
        for vertices, faces, flip in meshes:
            for _, a, b, c in faces:
                if flip:
                    b, c = c, b
                f.write(f"3 {a + offset} {b + offset} {c + offset}\n")
            offset += len(vertices)
    return nt
