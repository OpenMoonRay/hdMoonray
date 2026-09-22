# Copyright 2024-2025 DreamWorks Animation LLC
# SPDX-License-Identifier: Apache-2.0

# -*- coding: utf-8 -*-
import os
import sys

unittestflags = (['@run_all', '--unittest-xml']
                 if os.environ.get('BROKEN_CUSTOM_ARGS_UNITTESTS') else [])
ratsflags = (['@rats'] if os.environ.get('BROKEN_CUSTOM_ARGS_RATS') else [])

name = 'hdMoonray'

if 'early' not in locals() or not callable(early):
    def early(): return lambda x: x

@early()
def version():
    _version = '10.3'
    from rezbuild import earlybind
    return earlybind.version(this, _version)

description = "Hydra delegate for Moonray"

authors = [
    'DreamWorks Animation PSW - Hydra Moonray Team',
    'moonbase-dev@dreamworks.com',
    'hd-moonray@dreamworks.com'
]

help = ('For assistance, '
        "please contact the folio's owner at: moonbase-dev@dreamworks.com")

build_system = 'cmake'

variants = [
    [   # variant 0
        'os-rocky-9',
        'refplat-vfx2025.0',
        'usd_imaging-0.25.5.1.x',
        'openimageio-3.0',
        'opt_level-optdebug',
        'python-3.11'
    ],
    [   # variant 1
        'os-rocky-9',
        'refplat-houdini21.0',
        'usd_imaging-0.25.5.1.x.5',
        'openimageio-3.0',
        'opt_level-optdebug',
        'python-3.11'
    ],
    [   # variant 2
        'os-rocky-9',
        'refplat-vfx2026.0',
        'usd_imaging-0.26.5.x',
        'openimageio-3.1',
        'opt_level-optdebug',
        'python-3.13'
    ],
    #[   # variant 3
    #    'os-rocky-9',
    #    'refplat-vfx2025.0',
    #    'usd_imaging-0.25.11',
    #    'openimageio-3.0',
    #    'opt_level-optdebug',
    #    'python-3.11'
    #],
]

conf_CI_variants = variants

requires = [
    'moonray-18.4',
    'moonshine_dwa-15.6',
    'moonshine-15.6',
    'mcrt_computation-16.4',
    'arras4_core-4.10',
    'mcrt_messages-15.0',
    'mcrt_dataio-16.2',
    'mkl'
]

private_build_requires = [
    'cmake_modules-2.0',
    'gcc-11.x|14.2.x',
    'cppunit'
]

def commands():
    prependenv('PXR_PLUGINPATH_NAME', '{root}/plugin/pxr') # usd_core-0.23.5 and later
    prependenv('PXR_PLUGIN_PATH', '{root}/plugin/pxr') # usd_core-0.23.2 and earlier
    prependenv('PATH', '{root}/bin')
    prependenv('LD_LIBRARY_PATH', '{root}/lib64')
    prependenv('ARRAS_SESSION_PATH', '{root}/sessions/dwa')
    prependenv('HOUDINI_PATH', '{root}/plugin/houdini')
    prependenv('HDMOONRAY_DOUBLESIDED', '1')
    setenv('RATS_CANONICAL_PATH', '/work/rd/raas/hydra/rats/canonicals/')
    setenv('RATS_TESTSUITE_PATH', '{root}/testSuite')

config_version = 0
