
version_deps += custom_target('version information',
    command : [preprocess_command, '@INPUT@', '@OUTPUT@'],
    build_always : true,
    input : 'version_auto.c.in',
    output : 'version_auto.c',
)

if host_machine.system() == 'windows'
    winmod = import('windows')

    rcpath = join_paths(meson.current_build_dir(), 'taisei.rc')
    rcdefs = [
        '-DICONS_DIR=@0@'.format(join_paths(meson.source_root(), 'misc', 'icons'))
    ]

    if is_debug_build
        rcdefs += ['-DBUILDTYPE_DEFINE=#define DEBUG_BUILD']
    else
        rcdefs += ['-DBUILDTYPE_DEFINE=#define RELEASE_BUILD']
    endif

    # https://github.com/mesonbuild/meson/issues/4301
    rc_target = custom_target('windows-resource',
        command : [preprocess_command, rcdefs, '@INPUT@', '@OUTPUT@'],
        build_always : true,
        input : 'taisei.rc.in',
        output : 'taisei.rc',
    )

    version_deps += winmod.compile_resources(rc_target)

    # msvcrt is dumb and only supports up to c89.
    # with this defined, alternative implementations from mingw for e.g. the
    # printf family of functions will be used, which conform to c11.
    config.set('__USE_MINGW_ANSI_STDIO', 1)
endif

use_intel_intrin = get_option('intel_intrin') and cc.links('''
#include <immintrin.h>
__attribute__((target("sse4.2")))
int main(int argc, char **argv) {
    return _mm_crc32_u8(argc, 42);
}
''', name : 'SSE 4.2 intrinsics test')

taisei_src = files(
    'aniplayer.c',
    'audio_common.c',
    'boss.c',
    'cli.c',
    'color.c',
    'color.c',
    'config.c',
    'credits.c',
    'dialog.c',
    'difficulty.c',
    'ending.c',
    'enemy.c',
    'entity.c',
    'events.c',
    'framerate.c',
    'gamepad.c',
    'global.c',
    'hashtable.c',
    'hirestime.c',
    'item.c',
    'laser.c',
    'list.c',
    'log.c',
    'main.c',
    'objectpool_util.c',
    'player.c',
    'plrmodes.c',
    'progress.c',
    'projectile.c',
    'projectile_prototypes.c',
    'random.c',
    'refs.c',
    'replay.c',
    'stage.c',
    'stagedraw.c',
    'stageobjects.c',
    'stagetext.c',
    'stageutils.c',
    'taskmanager.c',
    'transition.c',
    'version.c',
    'video.c',
)

if get_option('objpools')
    taisei_src += files(
        'objectpool.c',
    )
else
    taisei_src += files(
        'objectpool_fake.c',
    )
endif

sse42_src = []

subdir('dialog')
subdir('menu')
subdir('plrmodes')
subdir('renderer')
subdir('resource')
subdir('rwops')
subdir('stages')
subdir('util')
subdir('vfs')

if use_intel_intrin
    sse42_lib = static_library(
        'taisei_sse42',
        sse42_src,
        c_args : taisei_c_args + ['-msse4.2'],
        install : false
    )
    sse42_dep = declare_dependency(link_with: sse42_lib)
    taisei_deps += sse42_dep
    config.set('TAISEI_BUILDCONF_USE_SSE42', true)
    message('SSE 4.2 intrinsics will be used')
elif get_option('intel_intrin')
    config.set('TAISEI_BUILDCONF_USE_SSE42', false)
    warning('SSE 4.2 intrinsics can not be used')
endif

configure_file(configuration : config, output : 'build_config.h')

taisei_src += [
    dialog_src,
    menu_src,
    plrmodes_src,
    renderer_src,
    resource_src,
    rwops_src,
    stages_src,
    util_src,
    vfs_src,
]

if taisei_deps.contains(dep_sdl2_mixer)
    taisei_src += files(
        'audio_mixer.c',
    )
else
    taisei_src += files(
        'audio_null.c',
    )
endif

taisei_deps += renderer_deps

if macos_app_bundle
    taisei_exe_name = 'Taisei'
else
    taisei_exe_name = 'taisei'
endif

taisei_exe = executable(taisei_exe_name, taisei_src, version_deps,
    dependencies : taisei_deps,
    c_args : taisei_c_args,
    gui_app : not get_option('win_console'),
    install : true,
    install_dir : bindir,
)
