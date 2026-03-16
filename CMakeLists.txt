# The following five lines of boilerplate have to be in your project's
# CMakeLists in this exact order for cmake to work correctly
cmake_minimum_required(VERSION 3.16)

# 设置额外的组件目录，包含 BSP 和 MiddleWare
set(EXTRA_COMPONENT_DIRS 
    Components/BSP
    # Components/MiddleWare  # 如果不需要或目录为空，可以注释掉
)

# 修正拼写错误：去掉多余的减号
add_compile_options(-fdiagnostics-color=always)

include($ENV{IDF_PATH}/tools/cmake/project.cmake)
project(00_basic)