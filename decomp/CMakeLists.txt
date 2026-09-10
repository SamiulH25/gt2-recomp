# Allow building decomp/ standalone or as part of the repo root.
if(CMAKE_SOURCE_DIR STREQUAL CMAKE_CURRENT_SOURCE_DIR)
  cmake_minimum_required(VERSION 3.20)
  project(GT2Decomp LANGUAGES C)
  set(CMAKE_C_STANDARD 11)
endif()

enable_testing()

add_library(gt2_cd STATIC
  src/cd/cd.c
)
target_include_directories(gt2_cd PUBLIC include)

add_library(gt2_vol STATIC
  src/vol/vol.c
)
target_include_directories(gt2_vol PUBLIC include)
target_link_libraries(gt2_vol PUBLIC gt2_cd)

add_library(gt2_iso STATIC
  src/iso/iso.c
)
target_include_directories(gt2_iso PUBLIC include)
target_link_libraries(gt2_iso PUBLIC gt2_cd)

find_package(ZLIB REQUIRED)
add_library(gt2_ovl STATIC
  src/ovl/ovl.c
)
target_include_directories(gt2_ovl PUBLIC include)
target_link_libraries(gt2_ovl PUBLIC gt2_iso ZLIB::ZLIB)

add_library(gt2_save STATIC
  src/save/crc32.c
)
target_include_directories(gt2_save PUBLIC include)

add_library(gt2_spu STATIC
  src/spu/spu.c
)
target_include_directories(gt2_spu PUBLIC include)

add_library(gt2_mcd STATIC
  src/mcd/mcd.c
)
target_include_directories(gt2_mcd PUBLIC include)

add_library(gt2_boot STATIC
  src/boot/boot.c
)
target_include_directories(gt2_boot PUBLIC include)
target_link_libraries(gt2_boot PUBLIC gt2_cd gt2_iso gt2_vol gt2_ovl)

add_library(gt2_car STATIC
  src/car/car.c
)
target_include_directories(gt2_car PUBLIC include)
target_link_libraries(gt2_car PUBLIC gt2_vol)

add_library(gt2_asset STATIC
  src/asset/asset.c
)
target_include_directories(gt2_asset PUBLIC include)
target_link_libraries(gt2_asset PUBLIC gt2_vol)

add_library(gt2_task STATIC
  src/task/task.c
)
target_include_directories(gt2_task PUBLIC include)
target_link_libraries(gt2_task PUBLIC gt2_car gt2_asset)

add_library(gt2_host STATIC
  src/host/host.c
)
target_include_directories(gt2_host PUBLIC include)

add_library(gt2_menu STATIC
  src/menu/menu.c
)
target_include_directories(gt2_menu PUBLIC include)

add_library(gt2_dispatch STATIC
  src/dispatch/dispatch.c
)
target_include_directories(gt2_dispatch PUBLIC include)

add_library(gt2_render STATIC
  src/render/render.c
)
target_include_directories(gt2_render PUBLIC include)

add_library(gt2_tim STATIC
  src/tim/tim.c
)
target_include_directories(gt2_tim PUBLIC include)
target_link_libraries(gt2_tim PUBLIC ZLIB::ZLIB)

add_executable(test_vol tests/test_vol.c)
target_link_libraries(test_vol PRIVATE gt2_vol)

add_executable(test_vol_pack tests/test_vol_pack.c)
target_link_libraries(test_vol_pack PRIVATE gt2_vol)
add_executable(test_cd tests/test_cd.c)
target_link_libraries(test_cd PRIVATE gt2_cd)

add_executable(test_iso tests/test_iso.c)
target_link_libraries(test_iso PRIVATE gt2_iso)

add_executable(test_ovl tests/test_ovl.c)
target_link_libraries(test_ovl PRIVATE gt2_ovl)

add_executable(test_save tests/test_save.c)
target_link_libraries(test_save PRIVATE gt2_save)

add_executable(test_spu tests/test_spu.c)
target_link_libraries(test_spu PRIVATE gt2_spu)

add_executable(test_mcd tests/test_mcd.c)
target_link_libraries(test_mcd PRIVATE gt2_mcd gt2_save)

add_executable(test_boot tests/test_boot.c)
target_link_libraries(test_boot PRIVATE gt2_boot)

add_executable(test_car tests/test_car.c)
target_link_libraries(test_car PRIVATE gt2_car)

add_executable(test_asset tests/test_asset.c)
target_link_libraries(test_asset PRIVATE gt2_asset)

add_executable(test_task tests/test_task.c)
target_link_libraries(test_task PRIVATE gt2_task)

add_executable(test_host tests/test_host.c)
target_link_libraries(test_host PRIVATE gt2_host)

add_executable(test_menu tests/test_menu.c)
target_link_libraries(test_menu PRIVATE gt2_menu)

add_executable(test_dispatch tests/test_dispatch.c)
target_link_libraries(test_dispatch PRIVATE gt2_dispatch)

add_executable(test_render tests/test_render.c)
target_link_libraries(test_render PRIVATE gt2_render)

add_executable(test_tim tests/test_tim.c)
target_link_libraries(test_tim PRIVATE gt2_tim gt2_vol ZLIB::ZLIB)

add_executable(car_probe tests/car_probe.c)
target_link_libraries(car_probe PRIVATE gt2_car)

add_executable(test_sysclock tests/test_sysclock.c)
target_include_directories(test_sysclock PRIVATE include)

add_executable(path_probe tests/path_probe.c)
target_link_libraries(path_probe PRIVATE gt2_vol)

add_test(NAME vol COMMAND test_vol)
add_test(NAME vol_pack COMMAND test_vol_pack)
add_test(NAME tim COMMAND test_tim)
add_test(NAME cd COMMAND test_cd)
add_test(NAME iso COMMAND test_iso)
add_test(NAME ovl COMMAND test_ovl)
add_test(NAME save COMMAND test_save)
add_test(NAME spu COMMAND test_spu)
add_test(NAME mcd COMMAND test_mcd)
add_test(NAME boot COMMAND test_boot)
add_test(NAME car COMMAND test_car)
add_test(NAME sysclock COMMAND test_sysclock)
add_test(NAME asset COMMAND test_asset)
add_test(NAME task COMMAND test_task)
add_test(NAME host COMMAND test_host)
add_test(NAME menu COMMAND test_menu)
add_test(NAME dispatch COMMAND test_dispatch)
add_test(NAME render COMMAND test_render)
