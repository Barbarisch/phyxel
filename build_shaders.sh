#!/bin/bash

glslangValidator -V shaders/cube.vert -o shaders/cube.vert.spv
glslangValidator -V shaders/cube.frag -o shaders/cube.frag.spv
glslangValidator -V shaders/frustum_cull.comp -o shaders/frustum_cull.comp.spv