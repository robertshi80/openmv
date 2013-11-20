#include <stdlib.h>
#include <stm32f4xx_rcc.h>
#include <stm32f4xx_gpio.h>
#include <stm32f4xx_syscfg.h>
#include <stm32f4xx_misc.h>
#include <stm32f4xx_rcc.h>
#include <stm32f4xx_gpio.h>
#include <stm32f4xx_syscfg.h>
#include <stm32f4xx_misc.h>
#include "ov9650.h"
#include "rgb_led.h"
#include "usart.h"
#include "imlib.h"
#include "systick.h"
#define BREAK() __asm__ volatile ("BKPT");

int main(void)
{
    struct ov9650_handle ov9650;

    systick_init();

    /* init USART */
    usart_init(9600);

    /* init RGB LED module */
    rgb_led_init(LED_GREEN);

    /* init OV9650 module */
    ov9650_init(&ov9650);

    /* check MID,PID and VER */
    if (ov9650.id.MIDH != 0x7F ||
        ov9650.id.MIDL!= 0xA2 || 
        ov9650.id.VER != 0x52 ||
        ov9650.id.PID != 0x96) {
        goto error;
    }
   
    /* Set sensor brightness level -3..+3 */
    ov9650_set_brightness(&ov9650, 2);

    /* The sensor needs time to stablize especially 
       when using the automatic functions of the camera */
    rgb_led_set_color(LED_GREEN);
    systick_sleep(5000);
    rgb_led_set_color(LED_BLUE);

#if 0
    /* FPS test */
    while (1) {
        int fps = 0;
        uint32_t ticks = systick_current_millis();
        while (systick_current_millis()-ticks<1000) {
            ov9650_snapshot(&ov9650);
            fps++;
        }
        BREAK();
    }
#endif    

    while (1) {
        switch (usart_recv()) {
            case CMD_SET_PIXFORMAT:
                /* Configure image size and format and FPS */
                if (ov9650_set_pixformat(&ov9650, usart_recv()) != 0) {
                    goto error;
                }
                break;

            case CMD_SET_FRAMESIZE:
                /* Configure image size and format and FPS */
                if (ov9650_set_framesize(&ov9650, usart_recv()) != 0) {
                    goto error;
                }
                break;

            case CMD_SET_FRAMERATE:           
                /* Configure framerate */
                if (ov9650_set_framerate(&ov9650, usart_recv()) != 0) {
                    goto error;
                }
                break;

            case CMD_SNAPSHOT: {
                int i;
                struct frame_buffer *fb = &ov9650.frame_buffer;
                if (ov9650_snapshot(&ov9650) != 0) {
                    goto error;
                }

                for (i=0; i<(fb->width * fb->height * fb->bpp); i++) {
                    usart_send(fb->pixels[i]);
                }
                break;
            }
            case CMD_COLOR_TRACK: {
                struct point point= {0};
                struct frame_buffer *fb = &ov9650.frame_buffer;
                struct color hsv;
                #if 0
                /* red */
                struct color color= {
                    .h = 0,
                    .s = 70,
                    .v = 25
                };
                #endif
                hsv.h = usart_recv();
                hsv.s = usart_recv();
                hsv.v = usart_recv();

                if (ov9650_snapshot(&ov9650) != 0) {
                    goto error;
                }

                imlib_color_track(&ov9650.frame_buffer, &hsv, &point, 10);

                /* Send point coords from 0%..100% */
                usart_send(point.x*100/fb->width);
                usart_send(point.y*100/fb->height);
                break;
            }
            case CMD_MOTION_DETECTION: {
                int i;
                int pixels;
                struct frame_buffer *fb = &ov9650.frame_buffer;
                uint8_t *background = malloc(fb->width * fb->height * 1);//grayscale

                if (background == NULL) {
                    goto error;
                }

                if (ov9650.pixformat != PIXFORMAT_YUV422) {
                    /* Switch sensor to YUV422 to get 
                       a grayscale image from the Y channel */
//                    if (ov9650_config(&ov9650, OV9650_QQVGA_YUV422, OV9650_30FPS) != 0) {
 //                       goto error;
  //                  }
                }

                if (ov9650_snapshot(&ov9650) != 0) {
                    goto error;
                }

                /* Save this frame as background */
                for (i=0; i<(fb->width*fb->height); i++) {
                    background[i] = fb->pixels[i*2];
                }

                while (1) {
                    systick_sleep(1000);

                    if (ov9650_snapshot(&ov9650) != 0) {
                        goto error;
                    }
                    
                    for (i=0, pixels=0; i<(fb->width*fb->height); i++) {
                        uint8_t y = fb->pixels[i*2];
                        int diff = (y-background[i]) * (y-background[i]);

                        /* consider pixel changed if change more than 25% */
                        if ((diff*100)/(255*255) > 25) {
                            pixels++;
                            /* reuse the frame buffer */
                            fb->pixels[i] = 0xff;
                        } else {
                            fb->pixels[i] = 0x00;
                        }
                    }
                        
                    /* send if more than 10% of the image changed  */
                    if ((pixels*100)/(fb->width*fb->height)>5) {
                        uint8_t kernel[] = {1,1,1,
                                            1,1,1, 
                                            1,1,1};

                        /* free background frame */
                        free(background);

                        /* perform image erosion */
                        imlib_erosion_filter(fb, kernel, 3);

                        for (i=0; i<(fb->width*fb->height); i++) {
                            /* send twice because lcd expects RGB565 */
                            usart_send(fb->pixels[i]);
                            usart_send(fb->pixels[i]);
                        }
                        break;
                    }
                }
                break;
            }
        }
    }

error:
    rgb_led_set_color(LED_RED);
    while (1) {
        /* Do nothing */
    }
}