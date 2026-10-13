cmake_minimum_required(VERSION 3.16)
project(ModernPilkki CXX)
set(CMAKE_CXX_STANDARD 17)
find_package(SDL3 REQUIRED)
add_executable(pilkki main.cpp)
target_link_libraries(pilkki PRIVATE SDL3::SDL3)
add_executable(test_ves test_ves.cpp)
target_include_directories(test_ves PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
add_executable(test_spr test_spr.cpp)
target_include_directories(test_spr PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
add_executable(test_obj test_obj.cpp)
target_include_directories(test_obj PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
add_executable(test_audio test_audio.cpp)
target_include_directories(test_audio PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
add_executable(test_botinfo test_botinfo.cpp)
target_include_directories(test_botinfo PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
add_executable(test_net test_net.cpp)
target_include_directories(test_net PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
add_executable(test_pcx test_pcx.cpp)
target_include_directories(test_pcx PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
add_executable(test_model3ds test_model3ds.cpp)
target_include_directories(test_model3ds PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
add_executable(test_objects test_objects.cpp)
target_include_directories(test_objects PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
add_executable(test_input test_input.cpp)
target_include_directories(test_input PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(test_input PRIVATE SDL3::SDL3)
add_executable(test_tick test_tick.cpp)
target_include_directories(test_tick PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
add_executable(test_ui test_ui.cpp)
target_include_directories(test_ui PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(test_ui PRIVATE SDL3::SDL3)
add_executable(test_bots test_bots.cpp)
target_include_directories(test_bots PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
add_executable(test_render test_render.cpp)
target_include_directories(test_render PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(test_render PRIVATE SDL3::SDL3)
add_executable(test_game test_game.cpp)
target_include_directories(test_game PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
add_executable(test_menu test_menu.cpp)
target_include_directories(test_menu PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(test_menu PRIVATE SDL3::SDL3)
add_executable(test_terrain test_terrain.cpp)
target_include_directories(test_terrain PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(test_terrain PRIVATE SDL3::SDL3)
add_executable(test_camera test_camera.cpp)
target_include_directories(test_camera PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(test_camera PRIVATE SDL3::SDL3)
add_executable(test_models test_models.cpp)
target_include_directories(test_models PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(test_models PRIVATE SDL3::SDL3)
add_executable(test_angler test_angler.cpp)
target_include_directories(test_angler PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(test_angler PRIVATE SDL3::SDL3)
add_executable(sprview sprview.cpp)
target_include_directories(sprview PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(sprview PRIVATE SDL3::SDL3)
add_executable(objview objview.cpp)
target_include_directories(objview PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(objview PRIVATE SDL3::SDL3)
add_executable(lakeview lakeview.cpp)
target_include_directories(lakeview PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(lakeview PRIVATE SDL3::SDL3)
add_executable(test_state test_state.cpp)
target_include_directories(test_state PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
add_executable(render3d_view render3d_view.cpp)
target_include_directories(render3d_view PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(render3d_view PRIVATE SDL3::SDL3)
add_executable(test_render3d test_render3d.cpp)
target_include_directories(test_render3d PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(test_render3d PRIVATE SDL3::SDL3)

# Iteration 4: re-authored GLSL -> SPIR-V via glslc (clean-room; no DXBC is
# shipped). Skipped entirely when glslc is absent -- the GPU targets then just
# build without shaders (render3d_view falls back to clear-only).
find_program(GLSL_COMPILER NAMES glslc)
if(GLSL_COMPILER)
  set(SHADER_SRC_DIR ${CMAKE_CURRENT_SOURCE_DIR}/shaders)
  set(RENDER3D_VERT_SPV ${CMAKE_CURRENT_BINARY_DIR}/triangle.vert.spv)
  set(RENDER3D_FRAG_SPV ${CMAKE_CURRENT_BINARY_DIR}/triangle.frag.spv)
  set(RENDER3D_QUAD_VERT_SPV ${CMAKE_CURRENT_BINARY_DIR}/quad.vert.spv)
  set(RENDER3D_QUAD_FRAG_SPV ${CMAKE_CURRENT_BINARY_DIR}/quad.frag.spv)
  set(RENDER3D_MESH_VERT_SPV ${CMAKE_CURRENT_BINARY_DIR}/mesh.vert.spv)
  set(RENDER3D_MESH_FRAG_SPV ${CMAKE_CURRENT_BINARY_DIR}/mesh.frag.spv)
  add_custom_command(
    OUTPUT ${RENDER3D_VERT_SPV}
    COMMAND ${GLSL_COMPILER} -fshader-stage=vert ${SHADER_SRC_DIR}/triangle.vert
            -o ${RENDER3D_VERT_SPV}
    DEPENDS ${SHADER_SRC_DIR}/triangle.vert
    COMMENT "glslc triangle.vert")
  add_custom_command(
    OUTPUT ${RENDER3D_FRAG_SPV}
    COMMAND ${GLSL_COMPILER} -fshader-stage=frag ${SHADER_SRC_DIR}/triangle.frag
            -o ${RENDER3D_FRAG_SPV}
    DEPENDS ${SHADER_SRC_DIR}/triangle.frag
    COMMENT "glslc triangle.frag")
  add_custom_command(
    OUTPUT ${RENDER3D_QUAD_VERT_SPV}
    COMMAND ${GLSL_COMPILER} -fshader-stage=vert ${SHADER_SRC_DIR}/quad.vert
            -o ${RENDER3D_QUAD_VERT_SPV}
    DEPENDS ${SHADER_SRC_DIR}/quad.vert
    COMMENT "glslc quad.vert")
  add_custom_command(
    OUTPUT ${RENDER3D_QUAD_FRAG_SPV}
    COMMAND ${GLSL_COMPILER} -fshader-stage=frag ${SHADER_SRC_DIR}/quad.frag
            -o ${RENDER3D_QUAD_FRAG_SPV}
    DEPENDS ${SHADER_SRC_DIR}/quad.frag
    COMMENT "glslc quad.frag")
  add_custom_command(
    OUTPUT ${RENDER3D_MESH_VERT_SPV}
    COMMAND ${GLSL_COMPILER} -fshader-stage=vert ${SHADER_SRC_DIR}/mesh.vert
            -o ${RENDER3D_MESH_VERT_SPV}
    DEPENDS ${SHADER_SRC_DIR}/mesh.vert
    COMMENT "glslc mesh.vert")
  add_custom_command(
    OUTPUT ${RENDER3D_MESH_FRAG_SPV}
    COMMAND ${GLSL_COMPILER} -fshader-stage=frag ${SHADER_SRC_DIR}/mesh.frag
            -o ${RENDER3D_MESH_FRAG_SPV}
    DEPENDS ${SHADER_SRC_DIR}/mesh.frag
    COMMENT "glslc mesh.frag")
  add_custom_target(render3d_shaders DEPENDS ${RENDER3D_VERT_SPV}
                                             ${RENDER3D_FRAG_SPV}
                                             ${RENDER3D_QUAD_VERT_SPV}
                                             ${RENDER3D_QUAD_FRAG_SPV}
                                             ${RENDER3D_MESH_VERT_SPV}
                                             ${RENDER3D_MESH_FRAG_SPV})
  foreach(tgt render3d_view test_render3d pilkki)
    target_compile_definitions(${tgt} PRIVATE
      RENDER3D_VERT_SPV="${RENDER3D_VERT_SPV}"
      RENDER3D_FRAG_SPV="${RENDER3D_FRAG_SPV}"
      RENDER3D_QUAD_VERT_SPV="${RENDER3D_QUAD_VERT_SPV}"
      RENDER3D_QUAD_FRAG_SPV="${RENDER3D_QUAD_FRAG_SPV}"
      RENDER3D_MESH_VERT_SPV="${RENDER3D_MESH_VERT_SPV}"
      RENDER3D_MESH_FRAG_SPV="${RENDER3D_MESH_FRAG_SPV}")
    add_dependencies(${tgt} render3d_shaders)
  endforeach()
else()
  message(STATUS "glslc not found: 3D shader build skipped (clear-only viewer)")
endif()
