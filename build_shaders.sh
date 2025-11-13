#!/bin/bash

glslangValidator -V shaders/static_voxel.vert -o shaders/static_voxel.vert.spv
glslangValidator -V shaders/voxel.frag -o shaders/voxel.frag.spv
glslangValidator -V shaders/frustum_cull.comp -o shaders/frustum_cull.comp.spv