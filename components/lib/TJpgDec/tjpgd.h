/*----------------------------------------------------------------------------/
/ TJpgDec - 微型 JPEG 解码器 R0.03 头文件                      (C)ChaN, 2021
/----------------------------------------------------------------------------*/
#ifndef DEF_TJPGDEC
#define DEF_TJPGDEC

#ifdef __cplusplus
extern "C" {
#endif

#include "tjpgdcnf.h"
#include <string.h>

#if defined(_WIN32)	/* VC++ 或其他没有 stdint.h 的编译器 */
typedef unsigned char	uint8_t;
typedef unsigned short	uint16_t;
typedef short			int16_t;
typedef unsigned long	uint32_t;
typedef long			int32_t;
#else				/* 嵌入式平台 */
#include <stdint.h>
#endif

#if JD_FASTDECODE >= 1
typedef int16_t jd_yuv_t;
#else
typedef uint8_t jd_yuv_t;
#endif


/* 错误码 */
typedef enum {
	JDR_OK = 0,	/* 0: 成功 */
	JDR_INTR,	/* 1: 被输出函数中断 */	
	JDR_INP,	/* 2: 设备错误或输入流异常终止 */
	JDR_MEM1,	/* 3: 图像内存池不足 */
	JDR_MEM2,	/* 4: 输入流缓冲区不足 */
	JDR_PAR,	/* 5: 参数错误 */
	JDR_FMT1,	/* 6: 数据格式错误 (可能是损坏的数据) */
	JDR_FMT2,	/* 7: 格式正确但不支持 */
	JDR_FMT3	/* 8: 不支持的 JPEG 标准 */
} JRESULT;



/* 输出图像中的矩形区域 */
typedef struct {
	uint16_t left;		/* 左边界 */
	uint16_t right;		/* 右边界 */
	uint16_t top;		/* 上边界 */
	uint16_t bottom;	/* 下边界 */
} JRECT;



/* 解码器对象结构体 */
typedef struct JDEC JDEC;
struct JDEC {
	size_t dctr;				/* 输入缓冲区中可用的字节数 */
	uint8_t* dptr;				/* 当前数据读取指针 */
	uint8_t* inbuf;				/* 位流输入缓冲区 */
	uint8_t dbit;				/* wreg 中可用的位数或读取位掩码 */
	uint8_t scale;				/* 输出缩放比例 */
	uint8_t msx, msy;			/* MCU 大小，以块为单位 (宽度, 高度) */
	uint8_t qtid[3];			/* 各分量的量化表 ID，Y, Cb, Cr */
	uint8_t ncomp;				/* 颜色分量数 1:灰度, 3:彩色 */
	int16_t dcv[3];				/* 各分量的前一个 DC 元素 */
	uint16_t nrst;				/* 重启间隔 */
	uint16_t width, height;		/* 输入图像尺寸 (像素) */
	uint8_t* huffbits[2][2];	/* 霍夫曼位分布表 [id][dcac] */
	uint16_t* huffcode[2][2];	/* 霍夫曼码字表 [id][dcac] */
	uint8_t* huffdata[2][2];	/* 霍夫曼解码数据表 [id][dcac] */
	int32_t* qttbl[4];			/* 反量化表 [id] */
#if JD_FASTDECODE >= 1
	uint32_t wreg;				/* 工作移位寄存器 */
	uint8_t marker;				/* 检测到的标记 (0:无) */
#if JD_FASTDECODE == 2
	uint8_t longofs[2][2];		/* 长码的表偏移 [id][dcac] */
	uint16_t* hufflut_ac[2];	/* AC 短码的快速霍夫曼解码表 [id] */
	uint8_t* hufflut_dc[2];		/* DC 短码的快速霍夫曼解码表 [id] */
#endif
#endif
	void* workbuf;				/* IDCT 和 RGB 输出的工作缓冲区 */
	jd_yuv_t* mcubuf;			/* MCU 的工作缓冲区 */
	void* pool;					/* 可用内存池指针 */
	size_t sz_pool;				/* 内存池大小 (可用字节数) */
	size_t (*infunc)(JDEC*, uint8_t*, size_t);	/* JPEG 流输入函数指针 */
	void* device;				/* 本次会话的 I/O 设备标识符指针 */
};



/* TJpgDec API 函数 */
JRESULT jd_prepare (JDEC* jd, size_t (*infunc)(JDEC*,uint8_t*,size_t), void* pool, size_t sz_pool, void* dev);
JRESULT jd_decomp (JDEC* jd, int (*outfunc)(JDEC*,void*,JRECT*), uint8_t scale);


#ifdef __cplusplus
}
#endif

#endif /* _TJPGDEC */
