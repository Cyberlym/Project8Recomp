cmake_minimum_required(VERSION 3.24)
project(thps_p8_game_image LANGUAGES CXX)

add_library(thps_p8_game_image game_image_reader.cpp xdvdfs_reader.cpp xex_identity.cpp)
target_include_directories(thps_p8_game_image PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}/..")
target_compile_features(thps_p8_game_image PUBLIC cxx_std_23)

include(CTest)
if(BUILD_TESTING)
  add_executable(thps_p8_game_image_contract_test tests/game_image_reader_contract_test.cpp)
  target_link_libraries(thps_p8_game_image_contract_test PRIVATE thps_p8_game_image)
  add_test(NAME thps_p8_game_image_valid_test COMMAND thps_p8_game_image_contract_test valid)
  add_test(NAME thps_p8_game_image_corruption_test COMMAND thps_p8_game_image_contract_test corruption)
endif()
