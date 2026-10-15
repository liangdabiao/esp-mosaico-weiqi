idf_component_register(
    SRCS "main.c" "game.c" "wq_cjk_font.c"
         "wq/wq_engine.c" "wq/wq_content.c" "wq/wq_session.c" "wq/wq_progress.c"
         "wq/wq_store.c" "wq/wq_wrap.c" "wq/wq_volume.c"
         "wq/wq_sfx.c" "wq/rtttl_player.c"
    INCLUDE_DIRS "." "wq"
    # button: BOOT 键（返回）用的 iot_button/button_gpio；BSP 已把它拉进工程。
    REQUIRES mosaico_game_app mosaico_raylib_fast esp-gsp nvs_flash
             esp-mosaico-bsp esp_codec_dev button)

# GSP needs an image to bind the Raylib framebuffer. This generated placeholder
# is embedded in the application; no external game resource partition is used.
set(canvas_png "${CMAKE_CURRENT_BINARY_DIR}/canvas.png")
execute_process(COMMAND "${PYTHON}" "${CMAKE_CURRENT_LIST_DIR}/make_canvas_png.py"
    "${canvas_png}" RESULT_VARIABLE canvas_result)
if(NOT canvas_result EQUAL 0)
    message(FATAL_ERROR "Could not generate the game canvas placeholder")
endif()
set(CANVAS_IMAGE "${canvas_png}")
configure_file("scene.json.in" "${CMAKE_CURRENT_BINARY_DIR}/scene.json" @ONLY)
gsp_add_bundle(${COMPONENT_LIB}
    SCENES "${CMAKE_CURRENT_BINARY_DIR}/scene.json"
    PIXEL_FORMAT rgb565)
