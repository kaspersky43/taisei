#!/usr/bin/env python3

import rectpack
import argparse
import shutil
import subprocess
import re

from pathlib import (
    Path,
)

from tempfile import (
    TemporaryDirectory,
)

from contextlib import (
    ExitStack,
    suppress,
)

from concurrent.futures import (
    ThreadPoolExecutor,
)

from PIL import (
    Image,
)

from taiseilib.common import (
    run_main,
    update_text_file,
    TaiseiError,
)

texture_formats = ['png', 'webp']

re_comment = re.compile(r'#.*')
re_keyval = re.compile(r'([a-z0-9_-]+)\s*=\s*(.+)', re.I)


class ConfigSyntaxError(Exception):
    pass


def write_sprite_def(dst, texture, region, size, overrides=None):
    dst.parent.mkdir(exist_ok=True, parents=True)

    text = (
        '# Autogenerated by the atlas packer, do not modify\n\n'
        f'texture = {texture}\n'
        f'region_x = {region[0]}\n'
        f'region_y = {region[1]}\n'
        f'region_w = {region[2]}\n'
        f'region_h = {region[3]}\n'
        # f'w = {size[0]}\n'
        # f'h = {size[1]}\n'
    )

    if overrides is not None:
        text += f'\n# -- Pasted from the override file --\n\n{overrides.strip()}\n'

    update_text_file(dst, text)


def write_texture_def(dst, texture, texture_fmt, global_overrides=None, local_overrides=None):
    dst.parent.mkdir(exist_ok=True, parents=True)

    text = (
        '# Autogenerated by the atlas packer, do not modify\n\n'
        f'source = res/gfx/{texture}.{texture_fmt}\n'
    )

    if global_overrides is not None:
        text += f'\n# -- Pasted from the global override file --\n\n{global_overrides.strip()}\n'

    if local_overrides is not None:
        text += f'\n# -- Pasted from the local override file --\n\n{local_overrides.strip()}\n'

    update_text_file(dst, text)


def write_override_template(dst, size):
    dst.parent.mkdir(exist_ok=True, parents=True)

    text = (
        '# This file was generated automatically, because this sprite doesn\'t have a custom override file.\n'
        '# To override this sprite\'s parameters, edit this file, remove the .renameme suffix and this comment, then commit it to the repository.\n\n'
        '# Modify these to change the virtual size of the sprite\n'
        f'w = {size[0]}\n'
        f'h = {size[1]}\n'
    )

    update_text_file(dst.with_suffix(dst.suffix + '.renameme'), text)


def parse_sprite_conf(path):
    conf = {}

    with suppress(FileNotFoundError):
        with open(path, 'r') as f:
            for line in f.readlines():
                line = re_comment.sub('', line)
                line = line.strip()

                if not line:
                    continue

                try:
                    key, val = re_keyval.findall(line)[0]
                except IndexError:
                    raise ConfigSyntaxError(line)

                conf[key] = val

    return conf


def get_override_file_name(basename):
    if re.match(r'.*\.frame\d{4}$', basename):
        basename, _ = basename.rsplit('.', 1)
        basename += '.framegroup'

    return f'{basename}.spr'


def random_fill(img, region):
    import random
    from PIL import ImageDraw

    draw = ImageDraw.Draw(img)
    gen = lambda: tuple((lambda: random.randrange(100, 255))() for i in range(3))
    fill = *gen(), 255
    outline = *gen(), 255

    draw.rectangle((region[0], region[1], region[2] - 1, region[3] - 1), fill=fill, outline=outline)
    del draw


def gen_atlas(overrides, src, dst, binsize, atlasname, tex_format=texture_formats[0], border=1, force_single=False, crop=True, leanify=True):
    overrides = Path(overrides).resolve()
    src = Path(src).resolve()
    dst = Path(dst).resolve()

    sprite_configs = {}

    def get_border(sprite, default_border=border):
        return max(default_border, int(sprite_configs[sprite].get('border', default_border)))

    try:
        texture_local_overrides = (src / 'atlas.tex').read_text()
    except FileNotFoundError:
        texture_local_overrides = None

    try:
        texture_global_overrides = (overrides / 'atlas.tex').read_text()
    except FileNotFoundError:
        texture_global_overrides = None

    total_images = 0
    packed_images = 0
    rects = []

    for path in src.glob('**/*.*'):
        if path.is_file() and path.suffix[1:].lower() in texture_formats:
            img = Image.open(path)
            sprite_name = path.relative_to(src).with_suffix('').as_posix()
            sprite_config_path = overrides / (get_override_file_name(sprite_name) + '.conf')
            sprite_configs[sprite_name] = parse_sprite_conf(sprite_config_path)
            border = get_border(sprite_name)
            rects.append((img.size[0]+border*2, img.size[1]+border*2, (img, sprite_name)))

    total_images = len(rects)

    make_packer = lambda: rectpack.newPacker(
        # No rotation support in Taisei yet
        rotation=False,

        # Fine-tuned for least area used after crop
        sort_algo=rectpack.SORT_SSIDE,
        bin_algo=rectpack.PackingBin.BFF,
        pack_algo=rectpack.MaxRectsBl,
    )

    binsize = list(binsize)

    if force_single:
        while True:
            packer = make_packer()
            packer.add_bin(*binsize)

            for rect in rects:
                packer.add_rect(*rect)

            packer.pack()

            if sum(len(bin) for bin in packer) == total_images:
                break

            if binsize[1] < binsize[0]:
                binsize[1] *= 2
            else:
                binsize[0] *= 2
    else:
        packer = make_packer()

        for rect in rects:
            packer.add_rect(*rect)
            packer.add_bin(*binsize)

        packer.pack()

    packed_images = sum(len(bin) for bin in packer)

    if total_images != packed_images:
        missing = total_images - packed_images
        raise TaiseiError(
            f'{missing} sprite{"s were" if missing > 1 else " was"} not packed (bin size is too small?)'
        )

    with ExitStack() as stack:
        # Do everything in a temporary directory first
        temp_dst = Path(stack.enter_context(TemporaryDirectory(prefix=f'taisei-atlas-{atlasname}')))

        # Run multiple leanify processes in parallel, in case we end up with multiple pages
        # Yeah I'm too lazy to use Popen properly
        executor = stack.enter_context(ThreadPoolExecutor())

        for i, bin in enumerate(packer):
            textureid = f'atlas_{atlasname}_{i}'
            # dstfile = temp_dst / f'{textureid}.{tex_format}'
            # NOTE: we always save PNG first and convert with an external tool later if needed.
            dstfile = temp_dst / f'{textureid}.png'
            print(dstfile)

            dstfile_meta = temp_dst / f'{textureid}.tex'
            write_texture_def(dstfile_meta, textureid, tex_format, texture_global_overrides, texture_local_overrides)

            actual_size = [0, 0]

            if crop:
                for rect in bin:
                    if rect.x + rect.width > actual_size[0]:
                        actual_size[0] = rect.x + rect.width

                    if rect.y + rect.height > actual_size[1]:
                        actual_size[1] = rect.y + rect.height
            else:
                actual_size = (bin.width, bin.height)

            rootimg = Image.new('RGBA', tuple(actual_size), (0, 0, 0, 0))

            for rect in bin:
                rotated = False
                img, name = rect.rid
                border = get_border(name)

                if tuple(img.size) != (rect.width  - border*2, rect.height - border*2) and \
                   tuple(img.size) == (rect.height - border*2, rect.width  - border*2):
                    rotated = True

                region = (rect.x + border, rect.y + border, rect.x + rect.width - border, rect.y + rect.height - border)

                print(rect, region, name)

                if rotated:
                    rimg = img.transpose(Image.ROTATE_90)
                    rootimg.paste(rimg, region)
                    rimg.close()
                else:
                    rootimg.paste(img, region)

                img.close()
                # random_fill(rootimg, region)

                override_path = overrides / get_override_file_name(name)

                if override_path.exists():
                    override_contents = override_path.read_text()
                else:
                    override_contents = None
                    write_override_template(override_path, img.size)

                write_sprite_def(
                    temp_dst / f'{name}.spr',
                    textureid,
                    (region[0], region[1], region[2] - region[0], region[3] - region[1]),
                    img.size,
                    overrides=override_contents
                )

            print('Atlas texture area: ', rootimg.size[0] * rootimg.size[1])
            rootimg.save(dstfile)

            @executor.submit
            def postprocess(dstfile=dstfile):
                oldfmt = dstfile.suffix[1:].lower()

                if oldfmt != tex_format:
                    new_dstfile = dstfile.with_suffix(f'.{tex_format}')

                    if tex_format == 'webp':
                        subprocess.check_call([
                            'cwebp',
                            '-progress',
                            '-preset', 'drawing',
                            '-z', '9',
                            '-lossless',
                            '-q', '100',
                            str(dstfile),
                            '-o', str(new_dstfile),
                        ])
                    else:
                        raise TaiseiError(f'Unhandled conversion {oldfmt} -> {tex_format}')

                    dstfile.unlink()
                    dstfile = new_dstfile

                if leanify:
                    subprocess.check_call(['leanify', '-v', str(dstfile)])

        # Wait for subprocesses to complete
        executor.shutdown(wait=True)

        # Only now, if everything is ok so far, copy everything to the destination, possibly overwriting previous results
        pattern = re.compile(rf'^atlas_{re.escape(atlasname)}_\d+.({"|".join(texture_formats)})$')
        for path in dst.iterdir():
            if pattern.match(path.name):
                path.unlink()

        targets = list(temp_dst.glob('**/*'))

        for dir in (p.relative_to(temp_dst) for p in targets if p.is_dir()):
            (dst / dir).mkdir(parents=True, exist_ok=True)

        for file in (p.relative_to(temp_dst) for p in targets if not p.is_dir()):
            shutil.copyfile(str(temp_dst / file), str(dst / file))


def main(args):
    parser = argparse.ArgumentParser(description='Generate texture atlases and sprite definitions', prog=args[0])

    parser.add_argument('overrides_dir',
        help='Directory containing per-sprite override files; templates are created automatically for missing files',
        type=Path,
    )

    parser.add_argument('source_dir',
        help='Directory containing input textures (searched recursively)',
        type=Path,
    )

    parser.add_argument('dest_dir',
        help='Directory to dump the results into',
        type=Path,
    )

    parser.add_argument('--width', '-W',
        help='Base width of a single atlas bin (default: 2048)',
        default=2048,
        type=int
    )

    parser.add_argument('--height', '-H',
        help='Base height of a single atlas bin (default: 2048)',
        default=2048,
        type=int
    )

    parser.add_argument('--name', '-n',
        help='Unique identificator for this atlas (used to form the texture name), default is inferred from directory name',
        default=None,
        type=str,
    )

    parser.add_argument('--border', '-b',
        help='Add a protective border WIDTH pixels wide around each sprite (default: 1)',
        metavar='WIDTH',
        dest='border',
        type=int,
        default=1
    )

    parser.add_argument('--crop', '-c',
        help='Remove unused space from bins (default)',
        dest='crop',
        action='store_true',
        default=True,
    )

    parser.add_argument('--no-crop', '-C',
        help='Do not remove unused space from atlases',
        dest='crop',
        action='store_false',
        default=True,
    )

    parser.add_argument('--single', '-s',
        help='Package everything into a single texture, possibly extending its size; this can be slow (default)',
        dest='single',
        action='store_true',
        default=True
    )

    parser.add_argument('--multiple', '-m',
        help='Split the atlas across multiple textures if the sprites won\'t fit otherwise',
        dest='single',
        action='store_false',
        default=True
    )

    parser.add_argument('--leanify', '-l',
        help='Leanify atlases to save space; very slow (default)',
        dest='leanify',
        action='store_true',
        default=True
    )

    parser.add_argument('--no-leanify', '-L',
        help='Do not leanify atlases',
        dest='leanify',
        action='store_false',
        default=True
    )

    parser.add_argument('--format', '-f',
        help=f'Format of the atlas textures (default: {texture_formats[0]})',
        choices=texture_formats,
        default=texture_formats[0],
    )

    args = parser.parse_args()

    if args.name is None:
        args.name = args.source_dir.name

    gen_atlas(
        args.overrides_dir,
        args.source_dir,
        args.dest_dir,
        (args.width, args.height),
        tex_format=args.format,
        atlasname=args.name,
        border=args.border,
        force_single=args.single,
        crop=args.crop,
        leanify=args.leanify
    )


if __name__ == '__main__':
    run_main(main)
