#
# This file is the default set of rules to compile a Pebble application.
#
# Feel free to customize this to your needs.
#
import glob
import os.path
import shutil
import subprocess

top = '.'
out = 'build'


def options(ctx):
    ctx.load('pebble_sdk')


def configure(ctx):
    """
    This method is used to configure your build. ctx.load(`pebble_sdk`) automatically configures
    a build for each valid platform in `targetPlatforms`. Platform-specific configuration: add your
    change after calling ctx.load('pebble_sdk') and make sure to set the correct environment first.
    Universal configuration: add your change prior to calling ctx.load('pebble_sdk').
    """
    ctx.load('pebble_sdk')


def build(ctx):
    ctx.load('pebble_sdk')

    build_worker = os.path.exists('worker_src')
    binaries = []

    cached_env = ctx.env
    for platform in ctx.env.TARGET_PLATFORMS:
        ctx.env = ctx.all_envs[platform]
        ctx.set_group(ctx.env.PLATFORM_NAME)
        app_elf = '{}/pebble-app.elf'.format(ctx.env.BUILD_DIR)
        ctx.pbl_build(source=ctx.path.ant_glob('src/c/**/*.c'), target=app_elf, bin_type='app')

        if build_worker:
            worker_elf = '{}/pebble-worker.elf'.format(ctx.env.BUILD_DIR)
            binaries.append({'platform': platform, 'app_elf': app_elf, 'worker_elf': worker_elf})
            ctx.pbl_build(source=ctx.path.ant_glob('worker_src/c/**/*.c'),
                          target=worker_elf,
                          bin_type='worker')
        else:
            binaries.append({'platform': platform, 'app_elf': app_elf})
    ctx.env = cached_env

    ctx.set_group('bundle')
    ctx.pbl_bundle(binaries=binaries,
                   js=ctx.path.ant_glob(['src/pkjs/**/*.js',
                                         'src/pkjs/**/*.json',
                                         'src/common/**/*.js']),
                   js_entry_file='src/pkjs/index.js')

    ctx.add_post_fun(_minify_and_copy_pbw)


# Every build: drop the unused JS source map (pebble-tool always bundles it),
# minify the phone JS in the .pbw, and copy the .pbw to Nextcloud for
# sideloading (copy skipped on machines without that folder).
def _minify_and_copy_pbw(ctx):
    build_dir = ctx.bldnode.abspath()
    pbw = glob.glob(os.path.join(build_dir, '*.pbw'))[0]
    js = os.path.join(build_dir, 'pebble-js-app.js')
    # Map is already gone when the bundle wasn't rebuilt, so ignore failure.
    subprocess.call(['zip', '-dq', pbw, 'pebble-js-app.js.map'],
                    stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
    subprocess.check_call(['npx', '--yes', 'terser', js, '-c', '-m', '-o', js])
    subprocess.check_call(['zip', '-jq', pbw, js])
    dest = os.path.expanduser('~/Nextcloud/pbws')
    if os.path.isdir(dest):
        shutil.copy(pbw, dest)
        print('Minified {} and copied to {}'.format(os.path.basename(pbw), dest))
