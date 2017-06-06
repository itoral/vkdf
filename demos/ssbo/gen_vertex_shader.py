# encoding=utf-8

"""Create vertex shader based on all the base type combination."""

from __future__ import print_function
import argparse
import os
import textwrap

from mako.template import Template

VERT_TEMPLATE = 'shader.vert.mako'
OUT_FILE = 'shader_'

def main():
    types = [['1', 'float'],
             ['2', 'vec2'],
             ['3', 'vec3'],
             ['4', 'vec4']]

    template_filepath = os.path.join(os.path.dirname(__file__), VERT_TEMPLATE)
    template = Template(filename=template_filepath,
                        strict_undefined=True)
    for t in types:
        out_filename = os.path.join(os.path.dirname(__file__), OUT_FILE+t[0]+'.vert')

        with open(out_filename, 'w') as out_file:
            out_file.write(template.render(type_name=t[1]));


if __name__ == '__main__':
    main()
