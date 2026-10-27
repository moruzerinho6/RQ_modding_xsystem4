version_h = vcs_tag(command : ['git', 'describe', 'HEAD'],
                    fallback : 'unknown version',
                    input : 'version.h.in',
                    output : 'version.h')

# sources for xsystem4
xsystem4 = [version_h,
            'audio.c',
            'audio_meta.c',
            'audio_mixer.c',
            'asset_manager.c',
            'base64.c',
            'cJSON.c',
            'draw.c',
            'effect.c',
            'ffi.c',
            'font_freetype.c',
            'font_fnl.c',
            'format.c',
            'hacks.c',
            'heap.c',
            'icon.c',
            'id_pool.c',
            'input.c',
            'json.c',
            'msgqueue.c',
            'page.c',
            'resume.c',
            'savedata.c',
            'scene.c',
            'screenshot.c',
            'sprite.c',
            'swf.c',
            'system4.c',
            'text.c',
            'util.c',
            'video.c',
            'win32_ui.c',
            'vm.c',

            '3d/collision.c',
            '3d/debug.c',
            '3d/model.c',
            '3d/mpr.c',
            '3d/parser.c',
            '3d/particle.c',
            '3d/reign.c',
            '3d/renderer.c',
            '3d/s3de.c',
            '3d/save.c',

            'dungeon/dgn.c',
            'dungeon/dtx.c',
            'dungeon/dungeon.c',
            'dungeon/generator.c',
            'dungeon/map.c',
            'dungeon/mtrand43.c',
            'dungeon/polyobj.c',
            'dungeon/renderer.c',
            'dungeon/skybox.c',
            'dungeon/tes.c',

            'parts/construction.c',
            'parts/debug.c',
            'parts/flash.c',
            'parts/flat.c',
            'parts/input.c',
            'parts/layoutbox.c',
            'parts/message.c',
            'parts/motion.c',
            'parts/parts.c',
            'parts/render.c',
            'parts/text.c',
            'parts/save.c',

            'hll/ACXLoader.c',
            'hll/ADVSYS.c',
            'hll/AFAFactory.c',
            'hll/AliceLogo.c',
            'hll/AliceLogo2.c',
            'hll/AliceLogo3.c',
            'hll/AliceLogo4.c',
            'hll/AliceLogo5.c',
            'hll/AnteaterADVEngine.c',
            'hll/Array.c',
            'hll/BanMisc.c',
            'hll/Bitarray.c',
            'hll/CalcTable.c',
            'hll/CGManager.c',
            'hll/CharSpriteManager.c',
            'hll/ChipmunkSpriteEngine.c',
            'hll/ChrLoader.c',
            'hll/CommonSystemData.c',
            'hll/Confirm.c',
            'hll/Confirm2.c',
            'hll/Confirm3.c',
            'hll/CrayfishLogViewer.c',
            'hll/Cursor.c',
            'hll/DALKDemo.c',
            'hll/DALKEDemo.c',
            'hll/Data.c',
            'hll/DataFile.c',
            'hll/Discord.c',
            'hll/DrawDungeon.c',
            'hll/DrawEffect.c',
            'hll/DrawGraph.c',
            'hll/DrawMovie.c',
            'hll/DrawNumeral.c',
            'hll/DrawPluginManager.c',
            'hll/DrawRain.c',
            'hll/DrawRipple.c',
            'hll/DrawSimpleText.c',
            'hll/DrawSnow.c',
            'hll/File.c',
            'hll/File2.c',
            'hll/FileOperation.c',
            'hll/FillAngle.c',
            'hll/GoatGUIEngine.c',
            'hll/Gpx2Plus.c',
            'hll/GUIEngine.c',
            'hll/HTTPDownloader.c',
            'hll/iarray.c',
            'hll/IbisInputEngine.c',
            'hll/InputDevice.c',
            'hll/InputString.c',
            'hll/KiwiSoundEngine.c',
            'hll/LoadCG.c',
            'hll/MADLoader.c',
            'hll/MainEXFile.c',
            'hll/MainSurface.c',
            'hll/MamanyoDemo.c',
            'hll/MamanyoSDemo.c',
            'hll/MarmotModelEngine.c',
            'hll/Math.c',
            'hll/MapLoader.c',
            'hll/MenuMsg.c',
            'hll/MonsterInfo.c',
            'hll/MsgLogManager.c',
            'hll/MsgLogViewer.c',
            'hll/MsgSkip.c',
            'hll/MusicSystem.c',
            'hll/NewFont.c',
            'hll/OutputLog.c',
            'hll/P3MapSprite.c',
            'hll/P3SquareSprite.c',
            'hll/PassRegister.c',
            'hll/PastelChime2.c',
            'hll/PartsEngine.c',
            'hll/PixelRestore.c',
            'hll/PlayDemo.c',
            'hll/PlayMovie.c',
            'hll/ReignEngine.c',
            'hll/SACT2.c',
            'hll/SengokuRanceFont.c',
            'hll/SoundFilePlayer.c',
            'hll/Sound2ex.c',
            'hll/StoatSpriteEngine.c',
            'hll/StretchHelper.c',
            'hll/SystemService.c',
            'hll/SystemServiceEx.c',
            'hll/Timer.c',
            'hll/Toushin3Loader.c',
            'hll/vmAnime.c',
            'hll/vmArray.c',
            'hll/vmCG.c',
            'hll/vmChrLoader.c',
            'hll/vmCursor.c',
            'hll/vmDalkGaiden.c',
            'hll/vmData.c',
            'hll/vmDialog.c',
            'hll/vmDrawGauge.c',
            'hll/vmDrawMsg.c',
            'hll/vmDrawNumber.c',
            'hll/vmFile.c',
            'hll/vmGraph.c',
            'hll/vmGraphQuake.c',
            'hll/vmKey.c',
            'hll/vmMapLoader.c',
            'hll/vmMsgLog.c',
            'hll/vmMusic.c',
            'hll/vmSound.c',
            'hll/vmSprite.c',
            'hll/vmString.c',
            'hll/vmSurface.c',
            'hll/vmSystem.c',
            'hll/vmTimer.c',
            'hll/ValueEncryption.c',
            'hll/VSFile.c',
]

xsystem4_deps = [libm, zlib, sdl2, ft2, ffi, cglm, sndfile, libsys4_dep] + gl_deps

if avcodec.found() and avformat.found() and avutil.found() and swscale.found()
    xsystem4 += 'movie_ffmpeg.c'
    xsystem4_deps += [avcodec, avformat, avutil, swscale]
else
    xsystem4 += 'movie_plmpeg.c'
endif

if get_option('debugger').allowed()
    add_project_arguments('-DDEBUGGER_ENABLED', language : 'c')
    xsystem4 += 'debug.c'
    xsystem4 += 'debug_info.c'
    xsystem4 += 'debugger_cmd.c'
    xsystem4 += 'debugger_dap.c'
    if chibi.found()
        xsystem4_deps += chibi
        xsystem4 += 'debugger_scm.c'
        add_project_arguments('-DHAVE_SCHEME', language : 'c')
    endif
    if readline.found()
        xsystem4_deps += readline
        add_project_arguments('-DHAVE_READLINE', language : 'c')
    endif
endif

static_link_args = []
if host_machine.system() == 'windows'
    static_link_args = ['-static', '-lstdc++', '-lcomdlg32', '-lcomctl32']
endif

winsys = 'console'
if get_option('buildtype').startswith('release')
    winsys = 'windows'
endif

exe_inc = [incdir]
exe_link = []

if get_option('profile')
    add_project_arguments('-DRQ_PROFILE', '-DRQ_PROFILE_FORCE', language : 'c')
    winsys = 'console'
elif get_option('modloader')
    add_project_arguments('-DRQ_PROFILE', language : 'c')
endif

if get_option('profile') or get_option('modloader')
    xsystem4 += 'profile.c'
endif

if get_option('modloader')
    add_project_arguments('-DRQ_MODLOADER', language : 'c')
    lua_inc = include_directories('../third_party/lua')
    lua_lib = static_library('lua54',
                             '../third_party/lua/onelua.c',
                             c_args : ['-DMAKE_LIB', '-Wno-unused-parameter',
                                       '-Wno-implicit-fallthrough'],
                             include_directories : lua_inc)
    py = find_program('python3', 'python', required: true)
    rq_stamp = custom_target(
        'rq_build_stamp_c',
        output: 'rq_build_stamp.c',
        command: [py, meson.current_source_dir() / 'gen_build_stamp.py', '@OUTPUT@'],
        build_always_stale: true)
    xsystem4 += ['modloader.c', 'modloader_agent.c', 'modloader_loot.c',
                 'modloader_hud.c', 'modloader_orb.c', 'modloader_skill.c',
                 'modloader_counter.c',
                 rq_stamp]
    exe_inc += lua_inc
    exe_link += lua_lib
endif

executable('xsystem4', xsystem4,
           dependencies : xsystem4_deps,
           c_args : ['-Wno-unused-parameter'],
           link_args : static_link_args,
           link_with : exe_link,
           include_directories : exe_inc,
           win_subsystem : winsys,
           install : true)
