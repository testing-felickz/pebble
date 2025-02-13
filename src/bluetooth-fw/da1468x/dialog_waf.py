# Copyright 2024 Google LLC
#
# Licensed under the Apache License, Version 2.0 (the "License");
# you may not use this file except in compliance with the License.
# You may obtain a copy of the License at
#
#     http://www.apache.org/licenses/LICENSE-2.0
#
# Unless required by applicable law or agreed to in writing, software
# distributed under the License is distributed on an "AS IS" BASIS,
# WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
# See the License for the specific language governing permissions and
# limitations under the License.

"""
Shared helper functions that are used in both boot/wscript and main/wscript.
FIXME: This file being full of poor practices and that the contents of this file
should not be taken as an example of the proper way to do anything in waf.
"""

import os
import waftools.c_preprocessor


def get_sdk_node(bld):
    return bld.path.find_node('../../vendor/bt-dialog-sdk')


def collect_sdk_sources(bld, sources_list):
    sdk_node = get_sdk_node(bld)
    return [sdk_node.find_node(s) for s in sources_list]


def generate_mem_ld(bld, config_h_path):
    mem_ld_node = bld.path.get_bld().make_node('mem.ld')
    mem_ld_h_path = bld.path.find_node('ldscripts/mem.ld.h')
    mem_map_h_path = bld.path.find_node('../../include/')
    mem_map_node = mem_map_h_path.find_node('da1468x_mem_map.h')

    cflags = []
    include_node = bld.path.find_node(config_h_path)
    include_path = include_node.abspath()
    cflags.append('-include%s' % include_path)

    sdk_config_include_path = get_sdk_node(bld).find_node(
        'sdk/bsp/config').abspath()
    cflags.append('-I%s' % sdk_config_include_path)
    cflags.append('-I%s' % mem_map_h_path.abspath())

    dialog_ic_step_define = bld.get_dialog_ic_step_define()
    cflags.append('-D%s' % dialog_ic_step_define)

    bld(rule=waftools.c_preprocessor.c_preproc,
        source=mem_ld_h_path, target=mem_ld_node, cflags=cflags)

    bld.add_manual_dependency(mem_ld_node, include_node)
    bld.add_manual_dependency(mem_ld_node, mem_map_node)
    bld.add_manual_dependency(mem_ld_node, dialog_ic_step_define)

    return mem_ld_node
