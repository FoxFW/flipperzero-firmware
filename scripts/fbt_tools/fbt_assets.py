import os
import subprocess
 
from ansi.color import fg
from SCons.Action import Action
from SCons.Builder import Builder
from SCons.Errors import StopError
from SCons.Node.FS import File
 
 
def _icons_emitter(target, source, env):
    icons_src = env.GlobRecursive("*.png", env["ICON_SRC_DIR"])
    icons_src += env.GlobRecursive("**/frame_rate", env["ICON_SRC_DIR"])
 
    target = [
        target[0].File(env.subst("${ICON_FILE_NAME}.c")),
        target[0].File(env.subst("${ICON_FILE_NAME}.h")),
    ]
    return target, icons_src
 
 
def _proto_emitter(target, source, env):
    target = []
    for src in source:
        basename = os.path.splitext(src.name)[0]
        target.append(env.File(f"compiled/{basename}.pb.c"))
        target.append(env.File(f"compiled/{basename}.pb.h"))
    return target, source
 
 
def _fox_emitter(target, source, env):
    res_root_dir = source[0].Dir(env["FOX_RES_TYPE"])
    source = list()
    source.extend(env.GlobRecursive("*.*", res_root_dir.srcnode()))
 
    target_base_dir = target[0]
    env.Replace(_FOX_OUT_DIR=target[0])
    env.Replace(_FOX_SRC_DIR=res_root_dir)
 
    if env["FOX_RES_TYPE"] == "external":
        target = [target_base_dir.File("manifest.txt")]
        ## A detailed list of files to be generated
        # Not used ATM, becasuse it inflates the internal dependency graph too much
        # Preserve original paths, do .png -> .bm conversion
        # target.extend(
        #     map(
        #         lambda node: target_base_dir.File(
        #             res_root_dir.rel_path(node).replace(".png", ".bm")
        #         ),
        #         filter(lambda node: isinstance(node, File), source),
        #     )
        # )
    else:
        asset_basename = f"assets_fox_{env['FOX_RES_TYPE']}"
        target = [
            target_base_dir.File(asset_basename + ".c"),
            target_base_dir.File(asset_basename + ".h"),
        ]
 
    ## Debug output
    # print(
    #     f"Fox res type: {env['FOX_RES_TYPE']},\ntarget files:",
    #     list(f.path for f in target),
    #     f"\nsource files:",
    #     list(f.path for f in source),
    # )
    return target, source
 
 
# Protobuf API version reported to companion apps (mobile app, qFlipper, etc.)
# over RPC (see applications/services/rpc/rpc_system.c).
#
# This used to be derived by running `git describe --tags --abbrev=0` inside
# the assets/protobuf submodule at build time. That is fragile: if the
# submodule is checked out without its tag history reachable (shallow clone,
# source built from a zip/tarball, some CI checkouts, etc.) `git describe`
# fails and this silently fell back to "0.0", which the official mobile app
# reports as an "Outdated firmware version" / unsupported firmware.
#
# Hardcoded instead, so a build's reported protobuf version no longer depends
# on git tag metadata being present at all. When assets/protobuf is updated,
# bump these two numbers to match the newest entry at the top of
# assets/protobuf/Changelog.
PROTOBUF_MAJOR_VERSION = 0
PROTOBUF_MINOR_VERSION = 27
 
 
def _proto_ver_generator(target, source, env):
    target_file = target[0]
 
    version_file_data = (
        "#pragma once",
        f"#define PROTOBUF_MAJOR_VERSION {PROTOBUF_MAJOR_VERSION}",
        f"#define PROTOBUF_MINOR_VERSION {PROTOBUF_MINOR_VERSION}",
        "",
    )
    with open(str(target_file), "wt") as file:
        file.write("\n".join(version_file_data))
 
 
def CompileIcons(env, target_dir, source_dir, *, icon_bundle_name="assets_icons"):
    return env.IconBuilder(
        target_dir,
        None,
        ICON_SRC_DIR=source_dir,
        ICON_FILE_NAME=icon_bundle_name,
    )
 
 
def generate(env):
    env.SetDefault(
        ASSETS_COMPILER="${FBT_SCRIPT_DIR}/assets.py",
        NANOPB_COMPILER="${ROOT_DIR}/lib/nanopb/generator/nanopb_generator.py",
    )
    env.AddMethod(CompileIcons)
 
    if not env["VERBOSE"]:
        env.SetDefault(
            ICONSCOMSTR="\tICONS\t${TARGET}",
            PROTOCOMSTR="\tPROTO\t${SOURCE}",
            FOXCOMSTR="\tFOX\t${FOX_RES_TYPE}",
            PBVERCOMSTR="\tPBVER\t${TARGET}",
        )
 
    env.Append(
        BUILDERS={
            "IconBuilder": Builder(
                action=Action(
                    [
                        [
                            "${PYTHON3}",
                            "${ASSETS_COMPILER}",
                            "icons",
                            "${ICON_SRC_DIR}",
                            "${TARGET.dir}",
                            "--filename",
                            "${ICON_FILE_NAME}",
                        ],
                    ],
                    "${ICONSCOMSTR}",
                ),
                emitter=_icons_emitter,
            ),
            "ProtoBuilder": Builder(
                action=Action(
                    [
                        [
                            "${PYTHON3}",
                            "${NANOPB_COMPILER}",
                            "-q",
                            "-I${SOURCE.dir.posix}",
                            "-D${TARGET.dir.posix}",
                            "${SOURCES.posix}",
                        ],
                    ],
                    "${PROTOCOMSTR}",
                ),
                emitter=_proto_emitter,
                suffix=".pb.c",
                src_suffix=".proto",
            ),
            "FoxExtBuilder": Builder(
                action=Action(
                    [
                        [
                            "${PYTHON3}",
                            "${ASSETS_COMPILER}",
                            "fox",
                            "${_FOX_SRC_DIR}",
                            "${_FOX_OUT_DIR}",
                        ],
                    ],
                    "${FOXCOMSTR}",
                ),
                emitter=_fox_emitter,
            ),
            "ProtoVerBuilder": Builder(
                action=Action(
                    _proto_ver_generator,
                    "${PBVERCOMSTR}",
                ),
            ),
        }
    )
 
 
def exists(env):
    return True
 