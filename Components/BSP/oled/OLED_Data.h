#ifndef OLED_DATA_H
#define OLED_DATA_H

#include <stdint.h>

/*ASCII字模数据*********************/

/*宽8像素，高16像素*/
extern const uint8_t OLED_F8x16[][16];

/*宽6像素，高8像素*/
extern const uint8_t OLED_F6x8[][6];

/*********************ASCII字模数据*/


/*汉字字模数据*********************/

/*相同的汉字只需要定义一次，汉字不分先后顺序*/
/*必须全部为汉字或者全角字符，不要加入任何半角字符*/

/*宽16像素，高16像素*/


/*********************汉字字模数据*/
typedef struct {
    const char Index[4];    // 汉字索引（UTF-8字符串）
    const uint8_t Data[32]; // 16x16点阵数据（32字节）
} ChineseCell_t;

#define OLED_CHN_CHAR_WIDTH 3  // UTF-8汉字宽度（字节数）

extern const ChineseCell_t OLED_CF16x16[];
/*图像数据*********************/

/*测试图像（一个方框，内部一个二极管符号），宽16像素，高16像素*/
extern const uint8_t Diode[];

/*Lil_jx笑脸头像，16*16*/
extern const uint8_t Lil_jx_16[];

/*Lil_jx笑脸头像，32*32*/
extern const uint8_t Lil_jx_32[];

/*按照上面的格式，在这个位置加入新的图像数据，且需要在OLED_Data.h中声明*/
//...

/*********************图像数据*/

#endif