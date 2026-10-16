"""Build RenderingDevice shader variants from the shared MAGES equations."""
import pathlib
import re
import sys

source_dir, output = map(pathlib.Path, sys.argv[1:])
names = [
    "AdditiveMaskedSprite", "CCMessageBoxSprite", "CHLCCMenuBackground",
    "ColorBurnMaskedSprite", "ColorDodgeMaskedSprite", "ColorMaskedSprite",
    "HardLightMaskedSprite", "LinearBurnMaskedSprite", "MaskedSprite",
    "MaskedSpriteBinary", "MaskedSpriteNoAlpha", "OverlayMaskedSprite",
    "ScreenMaskedSprite", "SoftLightMaskedSprite", "Sprite", "SpriteInverted",
    "YUVFrame", "GaussianBlur", "Mosaic", "NV12Frame", "SubtitleGlyph",
    "EdgeDetectedSingleSheetFont",
]
# std140: three matrices, then eight vec4s. All scalars are float-encoded,
# avoiding C++ bool/padding differences across compilers and architectures.
aliases = {
    "Projection": "mat4(p.v[0],p.v[1],p.v[2],p.v[3])",
    "Transformation": "mat4(p.v[4],p.v[5],p.v[6],p.v[7])",
    "SpriteTransformation": "mat4(p.v[4],p.v[5],p.v[6],p.v[7])",
    "MaskTransformation": "mat4(p.v[8],p.v[9],p.v[10],p.v[11])",
    "Alpha": "p.v[12].xy", "ColorShift": "p.v[13].xyz",
    "IsInverted": "(p.v[14].x != 0.0)",
    "IsSameTexture": "(p.v[14].y != 0.0)",
    "FullscreenMask": "(p.v[14].z != 0.0)",
    "IsAlpha": "(p.v[14].w != 0.0)",
    "TextureDimensions": "p.v[15].xy", "PixelOffset": "p.v[15].zw",
    "DifferenceFactor": "p.v[16].x", "IntensityShift": "p.v[16].y",
    "AlphaShift": "p.v[16].z", "HasMask": "(p.v[16].w != 0.0)",
    "IsHorizontal": "(p.v[17].x != 0.0)", "TileSize": "p.v[17].y",
}
samplers = {"ColorMap": 2, "Font": 2, "CoverageMap": 2, "Luma": 2,
            "Mask": 3, "Cb": 3, "CbCr": 3, "Cr": 4}
locations = {"uv": 0, "tint": 1, "maskUV": 2}
result = ["#pragma once\nnamespace Impacto::Host::Shaders {\n"]
for name in names:
    for stage in ("vert", "frag"):
        text = (source_dir / f"{name}_{stage}.glsl").read_text()
        header = "#version 450\nlayout(std140,set=0,binding=1) uniform Params { vec4 v[20]; } p;\n"
        for typ, var in re.findall(r"uniform\s+(\w+)\s+(\w+)\s*;", text):
            if typ == "sampler2D":
                header += f"layout(set=0,binding={samplers[var]}) uniform sampler2D {var};\n"
            else:
                value = aliases[var]
                if var == "Alpha" and name == "CCMessageBoxSprite":
                    value = "p.v[12]"
                if var == "Alpha" and name == "CHLCCMenuBackground":
                    value = "p.v[12].x"
                header += f"#define {var} ({value})\n"
        text = re.sub(r"uniform\s+\w+\s+\w+\s*;", "", text)
        text = re.sub(r"(?m)^(in|out) (\w+) (\w+);",
                      lambda m: f"layout(location={locations.get(m[3], 0)}) {m[1]} {m[2]} {m[3]};", text)
        text = text.replace("void main()", "void shared_main()")
        if stage == "vert":
            text = re.sub(r"layout\s*\(location\s*=\s*\d+\)\s*in\s+\w+\s+\w+;", "", text)
            header += """layout(std430,set=0,binding=0) readonly buffer Vertices { float data[]; } vertices;
vec2 Position; vec2 UV; vec4 Tint; vec2 MaskUV;
"""
            text += """
void main() {
    int i = gl_VertexIndex * 10;
    Position = vec2(vertices.data[i], vertices.data[i+1]);
    UV = vec2(vertices.data[i+2], vertices.data[i+3]);
    Tint = vec4(vertices.data[i+4],vertices.data[i+5],vertices.data[i+6],vertices.data[i+7]);
    MaskUV = vec2(vertices.data[i+8],vertices.data[i+9]);
    shared_main();
    gl_Position.y = -gl_Position.y;
    gl_Position.z = (gl_Position.z + gl_Position.w) * 0.5;
}
"""
        else:
            header += "layout(set=0,binding=5) uniform sampler2D stencilTexture;\n"
            color = "outColor" if name == "CHLCCMenuBackground" else "color"
            text += f"""
void main() {{
    if (p.v[18].x != 0.0 && texelFetch(stencilTexture,ivec2(gl_FragCoord.xy),0).r == 0.0) discard;
    if (p.v[18].y != 0.0) {{ {color} = vec4(1.0); return; }}
    shared_main();
}}
"""
        result.append(f'inline constexpr const char* {name}_{stage} = R"MAGES({header}{text})MAGES";\n')
# The 3D shaders share the original lighting, skinning and outline equations.
# Parameters are explicitly packed to keep the ABI independent of std140 bools.
model_aliases = {
    "ViewProjection": "mat4(p.v[0],p.v[1],p.v[2],p.v[3])",
    "MVP": "mat4(p.v[0],p.v[1],p.v[2],p.v[3])",
    "Model": "mat4(p.v[4],p.v[5],p.v[6],p.v[7])",
    "Tint": "p.v[8]", "WorldLightPosition": "p.v[9].xyz",
    "WorldEyePosition": "p.v[10].xyz", "DarkMode": "(p.v[11].x != 0.0)",
    "ModelOpacity": "p.v[11].y", "HasShadowColorMap": "(p.v[11].z != 0.0)",
}
def specialize_model(text, dash):
    # Only DASH conditions occur in the shared model equations. Resolve them
    # before emitting sampler bindings so the host binds precisely the active
    # material textures, including the RNE gradient/specular binding slots.
    active, stack, lines = True, [], []
    for line in text.splitlines():
        condition = line.strip()
        if condition.startswith(("#if ", "#ifdef ")):
            enabled = not dash if condition == "#if DASH == 0" else bool(dash)
            stack.append((active, enabled))
            active = active and enabled
        elif condition == "#else":
            parent, enabled = stack[-1]
            active = parent and not enabled
        elif condition == "#endif":
            active, _ = stack.pop()
        elif active:
            lines.append(line)
    return "\n".join(lines) + "\n"

for dash in (0, 1):
    for material in ("Character", "Eye", "Outline", "Background"):
        for stage in ("vert", "frag"):
            text = (source_dir / f"Renderable3D_{material}_{stage}.glsl").read_text()
            text = specialize_model(text, dash)
            header = ("#version 450\n#define UNIFORM_PRECISION\n"
                      + ("#define DASH 1\n" if dash else "") + "#define ModelMaxBonesPerMesh 32\n"
                      "layout(std140,set=0,binding=1) uniform Params { vec4 v[140]; } p;\n")
            text = re.sub(r"layout\(std140\) uniform \w+\s*\{.*?\};", "", text, flags=re.S)
            text = text.replace("uniform mat4 MVP;", "")
            for var, value in model_aliases.items():
                header += f"#define {var} ({value})\n"
            text = re.sub(r"Bones\[([^\]]+)\]", r"bone(\1)", text)
            header += "mat4 bone(int i) { int n=12+4*clamp(i,0,31); return mat4(p.v[n],p.v[n+1],p.v[n+2],p.v[n+3]); }\n"
            bindings = {"ColorMap": 2, "ShadowColorMap": 3,
                        "GradientMaskMap": 4 if dash else 3,
                        "SpecularColorMap": 5 if dash else 4,
                        "IrisColorMap": 2, "WhiteColorMap": 3,
                        "IrisSpecularColorMap": 4, "HighlightColorMap": 5,
                        "NoiseMap": 3}
            text = re.sub(r"uniform sampler2D (\w+);", lambda m:
                          f"layout(set=0,binding={bindings[m[1]]}) uniform sampler2D {m[1]};", text)
            locs = {"uv": 0, "worldFragPosition": 1, "worldNormal": 2, "uvNoise": 1, "color": 0}
            text = re.sub(r"(?m)^(in|out) (\w+) (\w+);", lambda m:
                          f"layout(location={locs[m[3]]}) {m[1]} {m[2]} {m[3]};", text)
            text = text.replace("void main()", "void shared_main()")
            if stage == "vert":
                text = re.sub(r"layout\s*\(location\s*=\s*\d+\)\s*in\s+(?:lowp\s+)?\w+\s+\w+;", "", text)
                header += ("layout(std430,set=0,binding=0) readonly buffer Vertices { float data[]; } vertices;\n"
                           "vec3 Position; vec3 Normal; vec2 UV; ivec4 BoneIndices; vec4 BoneWeights;\n")
                text += """
void main() {
    int i = gl_VertexIndex * 16;
    Position = vec3(vertices.data[i],vertices.data[i+1],vertices.data[i+2]);
    Normal = vec3(vertices.data[i+3],vertices.data[i+4],vertices.data[i+5]);
    UV = vec2(vertices.data[i+6],vertices.data[i+7]);
    BoneIndices = ivec4(vertices.data[i+8],vertices.data[i+9],vertices.data[i+10],vertices.data[i+11]);
    BoneWeights = vec4(vertices.data[i+12],vertices.data[i+13],vertices.data[i+14],vertices.data[i+15]);
    shared_main();
    gl_Position.y = -gl_Position.y;
    gl_Position.z = (gl_Position.z + gl_Position.w) * 0.5;
}
"""
            else:
                text += "\nvoid main() { shared_main(); }\n"
            result.append(f'inline constexpr const char* Model{material}{dash}_{stage} = R"MAGES({header}{text})MAGES";\n')
result.append("}\n")
output.parent.mkdir(parents=True, exist_ok=True)
content = "".join(result)
if not output.exists() or output.read_text() != content:
    output.write_text(content)
