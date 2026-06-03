idf_component_register(SRCS "g722_codec.c"
                            "opus_codec.c"
                            "aec_filter.c"
                            "wake_word.c"
                       INCLUDE_DIRS "."
                       REQUIRES esp-sr)
