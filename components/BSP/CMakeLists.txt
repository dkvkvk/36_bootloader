set(src_dirs
            IIC
            LED
            XL9555
            KEY
            ES8388
            I2S
            UART_AUDIO)

set(include_dirs
            IIC
            LED
            XL9555
            KEY
            ES8388
            I2S
            UART_AUDIO)

set(requires
            driver
            newlib
            esp_timer)

set(priv_requires
            espressif__esp_audio_codec)

idf_component_register(SRC_DIRS ${src_dirs} INCLUDE_DIRS ${include_dirs} REQUIRES ${requires} PRIV_REQUIRES ${priv_requires})