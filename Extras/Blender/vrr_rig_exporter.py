# Copyright (c) 2026 VR Robotica. Licensed under the MIT License (see LICENSE).
#
# VRR Rig Exporter - writes a JSON description of a hand-built Blender rig for the TechArt Toolbox "Rig" page in Unreal.
#
# Setup it understands (the usual Deform Rig + Control Rig split):
#   - A Deform armature whose deform bones follow a Control armature through Copy Transforms / Rotation / Location.
#   - The Control armature holds FK controls, IK chains (IK constraints with targets and poles), IK/FK blend bones
#     (several Copy Transforms whose influence is driven by a custom property) and helper/driver bones.
#   - A single armature that mixes deform and control bones also works (controls = bones with a custom shape).
#
# The JSON holds two layers:
#   "graph"   - everything raw: every control bone, its constraints, drivers, custom properties, shapes, limits.
#   "summary" - what Unreal needs: deform bones (with their Unreal names), IK chains, FK controls, IK/FK switches,
#               and which controls drive which deform bones.
#
# Use it as an add-on (Edit > Preferences > Add-ons > Install from Disk), from the Text Editor (Run Script),
# or headless:
#   blender -b file.blend --python vrr_rig_exporter.py -- [--deform RIG_DEFORM] [--control RIG_CONTROL] [--out path.json]
#
# Positions are in the armature's rest space, in meters (scene unit scale applied), Blender axes (Z up).
# The Unreal side matches them against the imported skeleton, so no axis conversion happens here.

bl_info = {
    "name": "VRR Rig Exporter",
    "author": "VR Robotica",
    "version": (1, 0, 0),
    "blender": (4, 2, 0),
    "location": "View3D > Sidebar > VRR",
    "description": "Export a Deform/Control rig description (.rig.json) for the Unreal Control Rig generator",
    "category": "Import-Export",
}

import json
import os
import re
import sys

import bpy
from bpy.props import BoolProperty, PointerProperty, StringProperty
from bpy_extras.io_utils import ExportHelper

FORMAT = "vrr_rig"
FORMAT_VERSION = 1
MAX_SHAPE_VERTS = 1024
COPY_TYPES = {"COPY_TRANSFORMS", "COPY_ROTATION", "COPY_LOCATION", "COPY_SCALE", "CHILD_OF", "ARMATURE"}
SIDE_PATTERNS = [
    (re.compile(r"[._ -](l|left)$", re.I), "left"),
    (re.compile(r"[._ -](r|right)$", re.I), "right"),
    (re.compile(r"^(l|left)[._ -]", re.I), "left"),
    (re.compile(r"^(r|right)[._ -]", re.I), "right"),
]


# ----------------------------------------------------------------------------------------------------------------
# Small helpers
# ----------------------------------------------------------------------------------------------------------------

def ue_name(name):
    """Bone name as Unreal's FBX import will show it (anything outside A-Z a-z 0-9 _ - becomes '_')."""
    return re.sub(r"[^A-Za-z0-9_\-]", "_", name)


def side_of(name):
    for pattern, side in SIDE_PATTERNS:
        if pattern.search(name):
            return side
    return "center"


def r(value, digits=5):
    return round(float(value), digits)


def vec(v, scale=1.0):
    return [r(c * scale) for c in v]


def matrix(m, scale=1.0):
    """4x4 row-major, translation scaled to meters."""
    rows = [[r(m[i][j]) for j in range(4)] for i in range(4)]
    for i in range(3):
        rows[i][3] = r(m[i][3] * scale)
    return rows


def plain(value):
    """Custom property value -> JSON-safe."""
    if hasattr(value, "to_dict"):
        return value.to_dict()
    if hasattr(value, "to_list"):
        return value.to_list()
    if isinstance(value, (int, float, str, bool)) or value is None:
        return value
    try:
        return [plain(v) for v in value]
    except TypeError:
        return str(value)


def visible_collections(arm):
    if hasattr(arm, "collections_all"):
        return {c.name for c in arm.collections_all if c.is_visible}
    return set()


def bone_collections(bone):
    return [c.name for c in getattr(bone, "collections", [])]


# ----------------------------------------------------------------------------------------------------------------
# Finding the rigs
# ----------------------------------------------------------------------------------------------------------------

def control_target_counts(deform_obj):
    """Armature objects that the deform bones copy from, with how many bones point at each."""
    counts = {}
    for pb in deform_obj.pose.bones:
        for con in pb.constraints:
            target = getattr(con, "target", None)
            if con.type in COPY_TYPES and target and target.type == 'ARMATURE' and target != deform_obj and not con.mute:
                counts[target] = counts.get(target, 0) + 1
    return counts


def guess_rigs(active):
    """(deform, control) for the active armature. Control can equal deform for single-armature rigs."""
    if active is None or active.type != 'ARMATURE':
        return None, None
    counts = control_target_counts(active)
    if counts:
        return active, max(counts, key=counts.get)
    # Active may be the control rig: find the armature whose deform bones follow it most
    best, best_count = None, 0
    for ob in bpy.data.objects:
        if ob.type == 'ARMATURE' and ob != active:
            count = control_target_counts(ob).get(active, 0)
            if count > best_count:
                best, best_count = ob, count
    if best:
        return best, active
    return active, active


# ----------------------------------------------------------------------------------------------------------------
# Export
# ----------------------------------------------------------------------------------------------------------------

class RigExporter:
    def __init__(self, deform_obj, control_obj, scene):
        self.deform = deform_obj
        self.control = control_obj or deform_obj
        self.single = self.control == self.deform
        self.scene = scene
        self.scale = scene.unit_settings.scale_length or 1.0
        self.warnings = []
        self.cbones = self.control.pose.bones
        self.visible = visible_collections(self.control.data)
        # IK targets and poles count as controls even when their bone collection is hidden
        self.ik_handles = set()
        self.chain_of = {}  # control bone -> IK chain it belongs to (filled during export)
        for pb in self.cbones:
            for con in pb.constraints:
                if con.type == 'IK' and not con.mute:
                    if con.target == self.control and con.subtarget:
                        self.ik_handles.add(con.subtarget)
                    if con.pole_target == self.control and con.pole_subtarget:
                        self.ik_handles.add(con.pole_subtarget)

    # -- raw graph -------------------------------------------------------------------------------------------------

    def constraint_info(self, con):
        info = {"name": con.name, "type": con.type, "influence": r(con.influence), "mute": con.mute}
        target = getattr(con, "target", None)
        if target:
            info["target"] = target.name
            sub = getattr(con, "subtarget", "")
            if sub:
                info["subtarget"] = sub
        for attr in ("owner_space", "target_space", "mix_mode", "use_x", "use_y", "use_z", "invert_x", "invert_y", "invert_z", "use_offset"):
            if hasattr(con, attr):
                info[attr] = plain(getattr(con, attr))
        if con.type == 'IK':
            info.update(chain_count=con.chain_count, use_tail=con.use_tail, use_stretch=con.use_stretch,
                        iterations=con.iterations, pole_angle=r(con.pole_angle))
            if con.pole_target:
                info["pole_target"] = con.pole_target.name
                if con.pole_subtarget:
                    info["pole_subtarget"] = con.pole_subtarget
        elif con.type == 'LIMIT_ROTATION':
            info["limits"] = {axis: {"use": getattr(con, "use_limit_" + axis), "min": r(getattr(con, "min_" + axis)), "max": r(getattr(con, "max_" + axis))}
                              for axis in "xyz"}
        elif con.type == 'LIMIT_LOCATION':
            info["limits"] = {axis: {"use_min": getattr(con, "use_min_" + axis), "use_max": getattr(con, "use_max_" + axis),
                                     "min": r(getattr(con, "min_" + axis) * self.scale), "max": r(getattr(con, "max_" + axis) * self.scale)}
                              for axis in "xyz"}
        return info

    def shape_info(self, pb):
        shape = pb.custom_shape
        if not shape:
            return None
        info = {
            "object": shape.name,
            "object_type": shape.type,
            "scale": vec(getattr(pb, "custom_shape_scale_xyz", (1, 1, 1))),
            "translation": vec(getattr(pb, "custom_shape_translation", (0, 0, 0)), self.scale),
            "rotation_euler": vec(getattr(pb, "custom_shape_rotation_euler", (0, 0, 0))),
            "use_bone_size": getattr(pb, "use_custom_shape_bone_size", True),
            "wire_width": r(getattr(pb, "custom_shape_wire_width", 1.0)),
            "transform_bone": pb.custom_shape_transform.name if getattr(pb, "custom_shape_transform", None) else None,
        }
        if shape.type == 'MESH' and len(shape.data.vertices) <= MAX_SHAPE_VERTS:
            info["vertices"] = [vec(v.co) for v in shape.data.vertices]
            info["edges"] = [list(e.vertices) for e in shape.data.edges]
        elif shape.type == 'EMPTY':
            info["empty_display"] = shape.empty_display_type
            info["empty_size"] = r(shape.empty_display_size)
        return info

    def color_info(self, pb):
        color = getattr(pb, "color", None)
        if color is None:
            return None
        info = {"palette": color.palette}
        if color.palette == 'CUSTOM':
            info["normal"] = vec(color.custom.normal)
            info["select"] = vec(color.custom.select)
            info["active"] = vec(color.custom.active)
        if color.palette == 'DEFAULT':
            bone_color = getattr(pb.bone, "color", None)
            if bone_color is not None and bone_color.palette != 'DEFAULT':
                info = {"palette": bone_color.palette, "from": "bone"}
                if bone_color.palette == 'CUSTOM':
                    info["normal"] = vec(bone_color.custom.normal)
        return info

    def custom_props(self, pb):
        props = {}
        for key in pb.keys():
            if key.startswith("_") or key == "rna_ui":
                continue
            entry = {"value": plain(pb[key])}
            try:
                ui = pb.id_properties_ui(key).as_dict()
                for field in ("min", "max", "soft_min", "soft_max", "default", "description"):
                    if field in ui:
                        entry[field] = plain(ui[field])
            except TypeError:
                pass
            props[key] = entry
        return props

    def bone_info(self, ob, bone):
        pb = ob.pose.bones[bone.name]
        m = bone.matrix_local
        info = {
            "name": bone.name,
            "parent": bone.parent.name if bone.parent else None,
            "deform": bone.use_deform,
            "side": side_of(bone.name),
            "collections": bone_collections(bone),
            "hidden": bone.hide,
            "head": vec(bone.head_local, self.scale),
            "tail": vec(bone.tail_local, self.scale),
            "roll_matrix": matrix(m, self.scale),
            "length": r(bone.length * self.scale),
            "use_connect": bone.use_connect,
            "inherit_rotation": bone.use_inherit_rotation,
            "inherit_scale": getattr(bone, "inherit_scale", "FULL"),
            "rotation_mode": pb.rotation_mode,
            "lock_location": list(pb.lock_location),
            "lock_rotation": list(pb.lock_rotation),
            "lock_scale": list(pb.lock_scale),
            "constraints": [self.constraint_info(c) for c in pb.constraints],
        }
        shape = self.shape_info(pb)
        if shape:
            info["shape"] = shape
        color = self.color_info(pb)
        if color and color.get("palette") != 'DEFAULT':
            info["color"] = color
        props = self.custom_props(pb)
        if props:
            info["properties"] = props
        return info

    def drivers(self, ob):
        result = []
        if not ob.animation_data:
            return result
        for fcurve in ob.animation_data.drivers:
            drv = fcurve.driver
            entry = {"data_path": fcurve.data_path, "index": fcurve.array_index, "type": drv.type,
                     "expression": drv.expression, "variables": []}
            match = re.match(r'pose\.bones\["(.+?)"\](?:\.constraints\["(.+?)"\])?\.?(.*)', fcurve.data_path)
            if match:
                entry["bone"], entry["constraint"], entry["property"] = match.group(1), match.group(2), match.group(3)
            for var in drv.variables:
                v = {"name": var.name, "type": var.type, "targets": []}
                for t in var.targets:
                    target = {"id": t.id.name if t.id else None}
                    if var.type == 'SINGLE_PROP':
                        target["data_path"] = t.data_path
                        prop = re.match(r'pose\.bones\["(.+?)"\]\["(.+?)"\]', t.data_path or "")
                        if prop:
                            target["prop_bone"], target["prop_name"] = prop.group(1), prop.group(2)
                    else:
                        target.update(bone=t.bone_target, transform_type=getattr(t, "transform_type", None),
                                      transform_space=getattr(t, "transform_space", None))
                    v["targets"].append(target)
                entry["variables"].append(v)
            result.append(entry)
        return result

    # -- analysis --------------------------------------------------------------------------------------------------

    def is_control(self, bone):
        """Animator-facing control: has a custom shape and sits in a visible bone collection (or no collections)."""
        pb = self.cbones[bone.name]
        if not pb.custom_shape or (self.single and bone.use_deform):
            return False
        if bone.name in self.ik_handles:
            return True
        names = bone_collections(bone)
        return not names or not self.visible or any(n in self.visible for n in names)

    def sources(self, pb):
        """Bones in the control armature that this pose bone copies from: [(constraint, bone name)]."""
        out = []
        for con in pb.constraints:
            target = getattr(con, "target", None)
            sub = getattr(con, "subtarget", "")
            if con.type in COPY_TYPES and target == self.control and sub and not con.mute:
                out.append((con, sub))
        return out

    def driving_controls(self, name, seen=None):
        """Controls that move this control-armature bone: the bone itself, its constraint sources, or its parents."""
        seen = seen if seen is not None else set()
        if name in seen or name not in self.cbones:
            return []
        seen.add(name)
        bone = self.control.data.bones[name]
        if self.is_control(bone):
            return [name]
        chain = self.chain_of.get(name)
        if chain:  # IK chain bones are moved by the chain's target and pole
            return [c for c in chain["target_controls"] + chain["pole_controls"]]
        found = []
        for _, sub in self.sources(self.cbones[name]):
            found += [c for c in self.driving_controls(sub, seen) if c not in found]
        if not found and bone.parent:
            found = self.driving_controls(bone.parent.name, seen)
        return found

    def export(self):
        deform_bones = [b for b in self.deform.data.bones if b.use_deform]
        if not deform_bones:
            raise RuntimeError(f"'{self.deform.name}' has no deform bones")

        # Deform bone -> control-armature bone it follows (the "driver" bone, e.g. DRV_upperarm.l)
        follows = {}
        ignored = []
        for bone in deform_bones:
            pb = self.deform.pose.bones[bone.name]
            if self.single:
                follows[bone.name] = bone.name
                continue
            best = None
            for con in pb.constraints:
                target = getattr(con, "target", None)
                if con.type not in COPY_TYPES or not target or con.mute:
                    continue
                if target == self.control and getattr(con, "subtarget", ""):
                    if best is None or (con.type == 'COPY_TRANSFORMS' and best.type != 'COPY_TRANSFORMS'):
                        best = con
                else:
                    ignored.append({"bone": bone.name, "constraint": con.name, "type": con.type, "target": target.name})
            if best:
                follows[bone.name] = best.subtarget
            else:
                self.warnings.append(f"Deform bone '{bone.name}' doesn't follow any bone in '{self.control.name}'")
        followed_by = {}
        for d, c in follows.items():
            followed_by.setdefault(c, []).append(d)

        drivers = self.drivers(self.control)
        driver_by_constraint = {(d.get("bone"), d.get("constraint")): d for d in drivers if d.get("constraint")}

        # IK chains
        ik_chains = []
        ik_bones = set()
        for pb in self.cbones:
            for con in pb.constraints:
                if con.type != 'IK' or con.mute:
                    continue
                chain = []
                bone = pb.bone if con.use_tail else pb.bone.parent
                count = con.chain_count if con.chain_count > 0 else 255
                while bone and len(chain) < count:
                    chain.append(bone.name)
                    bone = bone.parent
                chain.reverse()
                # Bones after the chain that ride along (e.g. IK_hand under IK_lowerarm)
                tip = [c.name for c in pb.bone.children if not self.is_control(c)] if con.use_tail else []
                ik_bones.update(chain + tip)
                target_bone = con.subtarget if con.target == self.control else None
                pole_bone = con.pole_subtarget if con.pole_target == self.control else None
                ik_chains.append({
                    "owner": pb.name, "chain": chain, "tip_children": tip,
                    "target": target_bone, "target_controls": self.driving_controls(target_bone) if target_bone else [],
                    "pole": pole_bone, "pole_controls": self.driving_controls(pole_bone) if pole_bone else [],
                    "pole_angle": r(con.pole_angle), "use_stretch": con.use_stretch, "iterations": con.iterations,
                    "side": side_of(pb.name),
                })
                if con.target and con.target != self.control:
                    self.warnings.append(f"IK on '{pb.name}' targets another object ({con.target.name}); not followed")
        ik_bones |= self.ik_handles
        self.chain_of = {b: c for c in ik_chains for b in c["chain"] + c["tip_children"]}

        # Deform bone -> IK/FK sources through its driver bone
        def deform_for(control_bone):
            """Deform bones that follow a control bone directly or through a blend bone that copies it."""
            result = list(followed_by.get(control_bone, []))
            for blend_name, blend_deforms in followed_by.items():
                blend = self.cbones.get(blend_name)
                if blend and any(sub == control_bone for _, sub in self.sources(blend)):
                    result += [d for d in blend_deforms if d not in result]
            return result

        for chain in ik_chains:
            chain["deform_chain"] = [d for b in chain["chain"] for d in deform_for(b)]
            chain["deform_tip"] = [d for b in chain["tip_children"] for d in deform_for(b)]
            if not chain["deform_chain"]:
                self.warnings.append(f"IK chain on '{chain['owner']}' doesn't reach any deform bone")

        # Per deform bone: what drives it, FK/IK sources, switch
        switches = {}
        deform_summary = []
        for bone in deform_bones:
            driver = follows.get(bone.name)
            entry = {"name": bone.name, "ue_name": ue_name(bone.name), "parent": bone.parent.name if bone.parent else None,
                     "ue_parent": ue_name(bone.parent.name) if bone.parent else None, "side": side_of(bone.name),
                     "head": vec(bone.head_local, self.scale), "tail": vec(bone.tail_local, self.scale),
                     "rest_matrix": matrix(bone.matrix_local, self.scale), "follows": driver}
            if driver and driver in self.cbones:
                dpb = self.cbones[driver]
                ik_src, fk_src, blend = [], [], []
                for con, sub in self.sources(dpb):
                    (ik_src if sub in ik_bones else fk_src).append(sub)
                    drv = driver_by_constraint.get((driver, con.name))
                    if drv and drv.get("property") == "influence":
                        for var in drv["variables"]:
                            for t in var["targets"]:
                                if t.get("prop_name"):
                                    key = f'{t["prop_bone"]}/{t["prop_name"]}'
                                    blend.append({"constraint": con.name, "source": sub, "mode": "ik" if sub in ik_bones else "fk",
                                                  "property": key, "expression": drv["expression"]})
                                    sw = switches.setdefault(key, {"bone": t["prop_bone"], "property": t["prop_name"], "drives": [],
                                                                   "one_means": None, "deform_bones": []})
                                    sw["drives"].append(f"{driver}:{con.name}")
                                    sw["deform_bones"].append(bone.name)
                                    sw["one_means"] = "fk" if sub not in ik_bones else "ik"
                entry["ik_sources"] = ik_src
                entry["fk_sources"] = fk_src
                entry["ik_controls"] = list(dict.fromkeys(c for s in ik_src for c in self.driving_controls(s) if c in self.ik_handles))
                entry["fk_controls"] = list(dict.fromkeys(c for s in fk_src for c in self.driving_controls(s)))
                if self.is_control(self.control.data.bones[driver]):
                    entry["fk_controls"] = [driver] + [c for c in entry["fk_controls"] if c != driver]
                if not entry["fk_controls"] and not ik_src:
                    entry["controls"] = self.driving_controls(driver)
                if blend:
                    entry["ik_fk_blend"] = blend
            deform_summary.append(entry)

        # Switch property values and side-label sanity check
        for key, sw in switches.items():
            pb = self.cbones.get(sw["bone"])
            if pb is not None and sw["property"] in pb.keys():
                sw["value"] = plain(pb[sw["property"]])
            bone_sides = {side_of(b) for b in sw["deform_bones"]} - {"center"}
            label = sw["property"]
            label_side = "left" if re.match(r"^(l|left)[ _./-]", label, re.I) else "right" if re.match(r"^(r|right)[ _./-]", label, re.I) else None
            if label_side and len(bone_sides) == 1 and label_side not in bone_sides:
                sw["label_mismatch"] = f"labelled {label_side} but drives {bone_sides.pop()} bones"
                self.warnings.append(f"Switch '{label}' {sw['label_mismatch']} (sides come from bone names)")

        controls = []
        for bone in self.control.data.bones:
            if not self.is_control(bone):
                continue
            pb = self.cbones[bone.name]
            drives = sorted({d["name"] for d in deform_summary
                             if bone.name in d.get("fk_controls", []) + d.get("ik_controls", []) + d.get("controls", [])})
            roles = []
            if any(bone.name == c["target"] for c in ik_chains):
                roles.append("ik_target")
            if any(bone.name == c["pole"] for c in ik_chains):
                roles.append("ik_pole")
            if any(bone.name in c["target_controls"] and bone.name != c["target"] for c in ik_chains):
                roles.append("ik_target_parent")
            if any(sw["bone"] == bone.name for sw in switches.values()):
                roles.append("switch")
            if bone.parent is None:
                roles.append("root")
            if bone.name not in self.ik_handles and any(bone.name in d.get("fk_controls", []) + d.get("controls", []) for d in deform_summary):
                roles.append("fk")
            controls.append({"name": bone.name, "role": roles[0] if roles else "other", "roles": roles or ["other"],
                             "side": side_of(bone.name), "parent": bone.parent.name if bone.parent else None,
                             "drives": drives, "has_limits": any(c.type.startswith("LIMIT_") for c in pb.constraints)})

        # Unreal import notes
        fbx_root = None if self.deform.name == "Armature" else self.deform.name
        if fbx_root:
            self.warnings.append(f"Blender's FBX export adds the armature object as a root bone named '{fbx_root}' "
                                 f"(rename the object to 'Armature' to avoid it)")
        if any(abs(s - 1.0) > 1e-4 for s in self.deform.scale):
            self.warnings.append(f"'{self.deform.name}' object scale is {tuple(round(s, 4) for s in self.deform.scale)}; apply it before export")
        if abs(self.scale - 1.0) > 1e-6:
            self.warnings.append(f"Scene unit scale is {self.scale}; positions here are converted to meters")

        return {
            "format": FORMAT, "format_version": FORMAT_VERSION,
            "source": {"blend_file": bpy.data.filepath, "blender_version": bpy.app.version_string,
                       "deform_armature": self.deform.name, "control_armature": self.control.name, "single_armature": self.single,
                       "unit_scale": self.scale, "units": "meters", "space": "armature_rest_blender_z_up",
                       "deform_object_matrix": matrix(self.deform.matrix_world, self.scale),
                       "control_object_matrix": matrix(self.control.matrix_world, self.scale),
                       "fps": self.scene.render.fps},
            "unreal": {"fbx_root_bone": ue_name(fbx_root) if fbx_root else None,
                       "bone_names": {b.name: ue_name(b.name) for b in deform_bones}},
            "summary": {"deform_bones": deform_summary, "ik_chains": ik_chains, "switches": list(switches.values()),
                        "controls": controls, "ignored_deform_constraints": ignored},
            "graph": {"bones": [self.bone_info(self.control, b) for b in self.control.data.bones],
                      "bone_collections": [{"name": c.name, "visible": c.is_visible} for c in getattr(self.control.data, "collections_all", [])],
                      "drivers": drivers},
            "warnings": self.warnings,
        }


def export_rig(deform_obj, control_obj, path, scene=None):
    data = RigExporter(deform_obj, control_obj, scene or bpy.context.scene).export()
    with open(path, "w", encoding="utf-8") as fh:
        json.dump(data, fh, indent=1)
    return data


def default_path(deform_obj):
    folder = os.path.dirname(bpy.data.filepath) if bpy.data.filepath else os.path.expanduser("~")
    return os.path.join(folder, f"{bpy.path.clean_name(deform_obj.name)}.rig.json")


# ----------------------------------------------------------------------------------------------------------------
# UI
# ----------------------------------------------------------------------------------------------------------------

def armature_poll(_self, ob):
    return ob.type == 'ARMATURE'


class VRR_RigExportSettings(bpy.types.PropertyGroup):
    deform: PointerProperty(name="Deform Rig", type=bpy.types.Object, poll=armature_poll,
                            description="Armature with the deform bones that get exported to Unreal")
    control: PointerProperty(name="Control Rig", type=bpy.types.Object, poll=armature_poll,
                             description="Armature the deform bones follow (same as Deform Rig for single-armature rigs)")


class VRR_OT_rig_detect(bpy.types.Operator):
    bl_idname = "vrr.rig_detect"
    bl_label = "Detect From Active"
    bl_description = "Fill in the Deform and Control rigs from the active armature"

    def execute(self, context):
        deform, control = guess_rigs(context.active_object)
        if not deform:
            self.report({'WARNING'}, "Select an armature first")
            return {'CANCELLED'}
        settings = context.scene.vrr_rig_export
        settings.deform, settings.control = deform, control
        self.report({'INFO'}, f"Deform: {deform.name}   Control: {control.name}")
        return {'FINISHED'}


class VRR_OT_rig_export(bpy.types.Operator, ExportHelper):
    bl_idname = "vrr.rig_export"
    bl_label = "Export Rig Description"
    bl_description = "Write a .rig.json describing the controls, IK chains and IK/FK switches for Unreal"
    filename_ext = ".json"
    filter_glob: StringProperty(default="*.json", options={'HIDDEN'})

    def invoke(self, context, event):
        settings = context.scene.vrr_rig_export
        if not settings.deform:
            settings.deform, settings.control = guess_rigs(context.active_object)
        if not settings.deform:
            self.report({'ERROR'}, "Pick a Deform Rig (or select an armature)")
            return {'CANCELLED'}
        self.filepath = default_path(settings.deform)
        return super().invoke(context, event)

    def execute(self, context):
        settings = context.scene.vrr_rig_export
        try:
            data = export_rig(settings.deform, settings.control or settings.deform, self.filepath, context.scene)
        except RuntimeError as err:
            self.report({'ERROR'}, str(err))
            return {'CANCELLED'}
        s = data["summary"]
        self.report({'INFO'}, f"Exported {len(s['deform_bones'])} deform bones, {len(s['controls'])} controls, "
                              f"{len(s['ik_chains'])} IK chains, {len(s['switches'])} switches"
                              + (f" ({len(data['warnings'])} warnings, see console)" if data["warnings"] else ""))
        for warning in data["warnings"]:
            print("VRR Rig Exporter:", warning)
        return {'FINISHED'}


class VRR_PT_rig_export(bpy.types.Panel):
    bl_label = "Rig Export"
    bl_space_type = 'VIEW_3D'
    bl_region_type = 'UI'
    bl_category = "VRR"

    def draw(self, context):
        settings = context.scene.vrr_rig_export
        col = self.layout.column()
        col.prop(settings, "deform")
        col.prop(settings, "control")
        col.operator(VRR_OT_rig_detect.bl_idname, icon='EYEDROPPER')
        col.separator()
        col.operator(VRR_OT_rig_export.bl_idname, icon='EXPORT')


def menu_export(self, _context):
    self.layout.operator(VRR_OT_rig_export.bl_idname, text="VRR Rig Description (.json)")


CLASSES = (VRR_RigExportSettings, VRR_OT_rig_detect, VRR_OT_rig_export, VRR_PT_rig_export)


def register():
    for cls in CLASSES:
        bpy.utils.register_class(cls)
    bpy.types.Scene.vrr_rig_export = PointerProperty(type=VRR_RigExportSettings)
    bpy.types.TOPBAR_MT_file_export.append(menu_export)


def unregister():
    bpy.types.TOPBAR_MT_file_export.remove(menu_export)
    del bpy.types.Scene.vrr_rig_export
    for cls in reversed(CLASSES):
        bpy.utils.unregister_class(cls)


# ----------------------------------------------------------------------------------------------------------------
# Headless / Run Script
# ----------------------------------------------------------------------------------------------------------------

def run_cli(argv):
    args = argv[argv.index("--") + 1:] if "--" in argv else []

    def arg(flag):
        return args[args.index(flag) + 1] if flag in args and args.index(flag) + 1 < len(args) else None

    deform = bpy.data.objects.get(arg("--deform")) if arg("--deform") else None
    control = bpy.data.objects.get(arg("--control")) if arg("--control") else None
    if deform is None:
        candidates = [ob for ob in bpy.data.objects if ob.type == 'ARMATURE' and control_target_counts(ob)]
        if control is not None:
            candidates = [ob for ob in candidates if control in control_target_counts(ob)]
        if not candidates:
            raise SystemExit("VRR Rig Exporter: no armature follows another armature; pass --deform NAME")
        deform = max(candidates, key=lambda ob: sum(control_target_counts(ob).values()))
    if control is None:
        control = guess_rigs(deform)[1]
    path = arg("--out") or default_path(deform)
    data = export_rig(deform, control, path, bpy.context.scene)
    s = data["summary"]
    print(f"VRR Rig Exporter: wrote {path}")
    print(f"  {deform.name} -> {control.name}: {len(s['deform_bones'])} deform bones, {len(s['controls'])} controls, "
          f"{len(s['ik_chains'])} IK chains, {len(s['switches'])} switches")
    for warning in data["warnings"]:
        print("  warning:", warning)


if __name__ == "__main__":
    if bpy.app.background:
        run_cli(sys.argv)
    else:
        register()
