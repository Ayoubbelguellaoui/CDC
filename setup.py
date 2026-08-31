#!/usr/bin/env python3
"""
OpenCDC Python Package Setup

This setup.py builds the OpenCDC Python bindings using pybind11.
"""

from setuptools import setup, Extension
from setuptools.command.build_ext import build_ext
import subprocess
import sys
import os
import re

def cmake_version():
    """Read the version from the root CMakeLists.txt — single source of truth."""
    here = os.path.dirname(os.path.abspath(__file__))
    with open(os.path.join(here, 'CMakeLists.txt'), 'r', encoding='utf-8') as f:
        m = re.search(r'project\s*\(\s*opencdc\s+VERSION\s+(\S+)', f.read())
    if not m:
        raise RuntimeError('cannot find project(opencdc VERSION ...) in CMakeLists.txt')
    return m.group(1)

class CMakeExtension(Extension):
    def __init__(self, name, sourcedir=''):
        Extension.__init__(self, name, sources=[])
        self.sourcedir = os.path.abspath(sourcedir)

class CMakeBuild(build_ext):
    def run(self):
        try:
            _ = subprocess.check_output(['cmake', '--version'])
        except OSError:
            raise RuntimeError("CMake must be installed to build OpenCDC")

        for ext in self.extensions:
            self.build_extension(ext)

    def build_extension(self, ext):
        extdir = os.path.abspath(os.path.dirname(self.get_ext_fullpath(ext.name)))
        
        cmake_args = [
            f'-DCMAKE_LIBRARY_OUTPUT_DIRECTORY={extdir}',
            f'-DPYTHON_EXECUTABLE={sys.executable}',
            '-DBUILD_PYTHON_BINDINGS=ON',
            '-DBUILD_TESTING=OFF',
        ]

        cfg = 'Debug' if self.debug else 'Release'
        build_args = ['--config', cfg]

        cmake_args += [f'-DCMAKE_BUILD_TYPE={cfg}']

        if not os.path.exists(self.build_temp):
            os.makedirs(self.build_temp)

        subprocess.check_call(['cmake', ext.sourcedir] + cmake_args,
                              cwd=self.build_temp)
        subprocess.check_call(['cmake', '--build', '.'] + build_args,
                              cwd=self.build_temp)

setup(
    name='opencdc',
    version=cmake_version(),
    author='OpenCDC Contributors',
    author_email='opencdc@example.com',
    description='Open-source static analysis tool for Clock Domain Crossing issues',
    long_description=open('README.md').read(),
    long_description_content_type='text/markdown',
    url='https://github.com/opencdc/opencdc',
    ext_modules=[CMakeExtension('opencdc')],
    cmdclass={'build_ext': CMakeBuild},
    zip_safe=False,
    python_requires='>=3.7',
    install_requires=[
        'pybind11>=2.6.0',
    ],
    classifiers=[
        'Development Status :: 4 - Beta',
        'Intended Audience :: Developers',
        'Intended Audience :: Science/Research',
        'License :: OSI Approved :: Apache Software License',
        'Programming Language :: Python :: 3',
        'Programming Language :: Python :: 3.7',
        'Programming Language :: Python :: 3.8',
        'Programming Language :: Python :: 3.9',
        'Programming Language :: Python :: 3.10',
        'Programming Language :: Python :: 3.11',
        'Programming Language :: C++',
        'Topic :: Scientific/Engineering :: Electronic Design Automation (EDA)',
        'Topic :: Software Development :: Quality Assurance',
    ],
    keywords='cdc clock-domain-crossing verification rtl systemverilog verilog',
)
