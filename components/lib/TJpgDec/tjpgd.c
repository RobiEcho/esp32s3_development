/*----------------------------------------------------------------------------/
/ TJpgDec - 微型 JPEG 解码器 R0.03                             (C)ChaN, 2021
/-----------------------------------------------------------------------------/
/ TJpgDec 是一个用于微型嵌入式系统的通用 JPEG 解码器模块。
/ 这是一个免费软件，开放用于教育、研究和商业开发，
/ 遵循以下条款的许可政策。
/
/  版权所有 (C) 2021, ChaN, 保留所有权利。
/
/ * TJpgDec 模块是免费软件，不提供任何担保。
/ * 使用无限制。您可以在自己的责任下将其用于个人、
/   非营利或商业产品，并可以修改和重新分发。
/ * 源代码的再分发必须保留上述版权声明。
/
/-----------------------------------------------------------------------------/
/ 2011年10月04日 R0.01  首次发布。
/ 2012年02月19日 R0.01a 修复了当扫描以转义序列开始时解压失败的问题。
/ 2012年09月03日 R0.01b 添加了 JD_TBLCLIP 选项。
/ 2019年03月16日 R0.01c 支持 stdint.h。
/ 2020年07月01日 R0.01d 修复了错误的整数类型使用。
/ 2021年05月08日 R0.02  支持灰度图像。分离配置选项。
/ 2021年06月11日 R0.02a 一些性能改进。
/ 2021年07月01日 R0.03  添加了 JD_FASTDECODE 选项。
/                       一些性能改进。
/----------------------------------------------------------------------------*/

#include "tjpgd.h"


#if JD_FASTDECODE == 2
#define HUFF_BIT	10	/* 应用快速霍夫曼解码的位长度 */
#define HUFF_LEN	(1 << HUFF_BIT)
#define HUFF_MASK	(HUFF_LEN - 1)
#endif


/*-----------------------------------------------*/
/* Z字形顺序到光栅顺序的转换表                    */
/*-----------------------------------------------*/

static const uint8_t Zig[64] = {	/* Z字形顺序到光栅顺序的转换表 */
	 0,  1,  8, 16,  9,  2,  3, 10, 17, 24, 32, 25, 18, 11,  4,  5,
	12, 19, 26, 33, 40, 48, 41, 34, 27, 20, 13,  6,  7, 14, 21, 28,
	35, 42, 49, 56, 57, 50, 43, 36, 29, 22, 15, 23, 30, 37, 44, 51,
	58, 59, 52, 45, 38, 31, 39, 46, 53, 60, 61, 54, 47, 55, 62, 63
};



/*-------------------------------------------------*/
/* Arai 算法的输入缩放因子                          */
/* (放大 16 位用于定点运算)                         */
/*-------------------------------------------------*/

static const uint16_t Ipsf[64] = {	/* 另见 aa_idct.png */
	(uint16_t)(1.00000*8192), (uint16_t)(1.38704*8192), (uint16_t)(1.30656*8192), (uint16_t)(1.17588*8192), (uint16_t)(1.00000*8192), (uint16_t)(0.78570*8192), (uint16_t)(0.54120*8192), (uint16_t)(0.27590*8192),
	(uint16_t)(1.38704*8192), (uint16_t)(1.92388*8192), (uint16_t)(1.81226*8192), (uint16_t)(1.63099*8192), (uint16_t)(1.38704*8192), (uint16_t)(1.08979*8192), (uint16_t)(0.75066*8192), (uint16_t)(0.38268*8192),
	(uint16_t)(1.30656*8192), (uint16_t)(1.81226*8192), (uint16_t)(1.70711*8192), (uint16_t)(1.53636*8192), (uint16_t)(1.30656*8192), (uint16_t)(1.02656*8192), (uint16_t)(0.70711*8192), (uint16_t)(0.36048*8192),
	(uint16_t)(1.17588*8192), (uint16_t)(1.63099*8192), (uint16_t)(1.53636*8192), (uint16_t)(1.38268*8192), (uint16_t)(1.17588*8192), (uint16_t)(0.92388*8192), (uint16_t)(0.63638*8192), (uint16_t)(0.32442*8192),
	(uint16_t)(1.00000*8192), (uint16_t)(1.38704*8192), (uint16_t)(1.30656*8192), (uint16_t)(1.17588*8192), (uint16_t)(1.00000*8192), (uint16_t)(0.78570*8192), (uint16_t)(0.54120*8192), (uint16_t)(0.27590*8192),
	(uint16_t)(0.78570*8192), (uint16_t)(1.08979*8192), (uint16_t)(1.02656*8192), (uint16_t)(0.92388*8192), (uint16_t)(0.78570*8192), (uint16_t)(0.61732*8192), (uint16_t)(0.42522*8192), (uint16_t)(0.21677*8192),
	(uint16_t)(0.54120*8192), (uint16_t)(0.75066*8192), (uint16_t)(0.70711*8192), (uint16_t)(0.63638*8192), (uint16_t)(0.54120*8192), (uint16_t)(0.42522*8192), (uint16_t)(0.29290*8192), (uint16_t)(0.14932*8192),
	(uint16_t)(0.27590*8192), (uint16_t)(0.38268*8192), (uint16_t)(0.36048*8192), (uint16_t)(0.32442*8192), (uint16_t)(0.27590*8192), (uint16_t)(0.21678*8192), (uint16_t)(0.14932*8192), (uint16_t)(0.07612*8192)
};



/*---------------------------------------------*/
/* 快速裁剪处理的转换表                         */
/*---------------------------------------------*/

#if JD_TBLCLIP

#define BYTECLIP(v) Clip8[(unsigned int)(v) & 0x3FF]

static const uint8_t Clip8[1024] = {
	/* 0..255 */
	0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15, 16, 17, 18, 19, 20, 21, 22, 23, 24, 25, 26, 27, 28, 29, 30, 31,
	32, 33, 34, 35, 36, 37, 38, 39, 40, 41, 42, 43, 44, 45, 46, 47, 48, 49, 50, 51, 52, 53, 54, 55, 56, 57, 58, 59, 60, 61, 62, 63,
	64, 65, 66, 67, 68, 69, 70, 71, 72, 73, 74, 75, 76, 77, 78, 79, 80, 81, 82, 83, 84, 85, 86, 87, 88, 89, 90, 91, 92, 93, 94, 95,
	96, 97, 98, 99, 100, 101, 102, 103, 104, 105, 106, 107, 108, 109, 110, 111, 112, 113, 114, 115, 116, 117, 118, 119, 120, 121, 122, 123, 124, 125, 126, 127,
	128, 129, 130, 131, 132, 133, 134, 135, 136, 137, 138, 139, 140, 141, 142, 143, 144, 145, 146, 147, 148, 149, 150, 151, 152, 153, 154, 155, 156, 157, 158, 159,
	160, 161, 162, 163, 164, 165, 166, 167, 168, 169, 170, 171, 172, 173, 174, 175, 176, 177, 178, 179, 180, 181, 182, 183, 184, 185, 186, 187, 188, 189, 190, 191,
	192, 193, 194, 195, 196, 197, 198, 199, 200, 201, 202, 203, 204, 205, 206, 207, 208, 209, 210, 211, 212, 213, 214, 215, 216, 217, 218, 219, 220, 221, 222, 223,
	224, 225, 226, 227, 228, 229, 230, 231, 232, 233, 234, 235, 236, 237, 238, 239, 240, 241, 242, 243, 244, 245, 246, 247, 248, 249, 250, 251, 252, 253, 254, 255,
	/* 256..511 */
	255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255,
	255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255,
	255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255,
	255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255,
	255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255,
	255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255,
	255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255,
	255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255,
	/* -512..-257 */
	0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
	0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
	0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
	0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
	0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
	0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
	0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
	0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
	/* -256..-1 */
	0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
	0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
	0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
	0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
	0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
	0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
	0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
	0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0
};

#else	/* JD_TBLCLIP */

static uint8_t BYTECLIP (int val)
{
	if (val < 0) return 0;
	if (val > 255) return 255;
	return (uint8_t)val;
}

#endif



/*-----------------------------------------------------------------------*/
/* 从内存池分配内存块                                                     */
/*-----------------------------------------------------------------------*/

static void* alloc_pool (	/* 返回分配的内存块指针 (NULL:无可用内存) */
	JDEC* jd,				/* 解码器对象指针 */
	size_t ndata			/* 要分配的字节数 */
)
{
	char *rp = 0;


	ndata = (ndata + 3) & ~3;			/* 将块大小对齐到字边界 */

	if (jd->sz_pool >= ndata) {
		jd->sz_pool -= ndata;
		rp = (char*)jd->pool;			/* 获取可用内存池的起始位置 */
		jd->pool = (void*)(rp + ndata);	/* 分配所需字节 */
	}

	return (void*)rp;	/* 返回分配的内存块 (NULL:无内存可分配) */
}




/*-----------------------------------------------------------------------*/
/* 使用 DQT 段创建反量化和预缩放表                                        */
/*-----------------------------------------------------------------------*/

static JRESULT create_qt_tbl (	/* 0:成功, !0:失败 */
	JDEC* jd,				/* 解码器对象指针 */
	const uint8_t* data,	/* 量化表指针 */
	size_t ndata			/* 输入数据大小 */
)
{
	unsigned int i, zi;
	uint8_t d;
	int32_t *pb;


	while (ndata) {	/* 处理段中的所有表 */
		if (ndata < 65) return JDR_FMT1;	/* 错误: 表大小未对齐 */
		ndata -= 65;
		d = *data++;							/* 获取表属性 */
		if (d & 0xF0) return JDR_FMT1;			/* 错误: 不是 8 位精度 */
		i = d & 3;								/* 获取表 ID */
		pb = alloc_pool(jd, 64 * sizeof (int32_t));/* 为表分配内存块 */
		if (!pb) return JDR_MEM1;				/* 错误: 内存不足 */
		jd->qttbl[i] = pb;						/* 注册表 */
		for (i = 0; i < 64; i++) {				/* 加载表 */
			zi = Zig[i];						/* Z字形顺序到光栅顺序转换 */
			pb[zi] = (int32_t)((uint32_t)*data++ * Ipsf[zi]);	/* 将 Arai 算法的缩放因子应用于反量化器 */
		}
	}

	return JDR_OK;
}




/*-----------------------------------------------------------------------*/
/* 使用 DHT 段创建霍夫曼码表                                              */
/*-----------------------------------------------------------------------*/

static JRESULT create_huffman_tbl (	/* 0:成功, !0:失败 */
	JDEC* jd,					/* 解码器对象指针 */
	const uint8_t* data,		/* 打包的霍夫曼表指针 */
	size_t ndata				/* 输入数据大小 */
)
{
	unsigned int i, j, b, cls, num;
	size_t np;
	uint8_t d, *pb, *pd;
	uint16_t hc, *ph;


	while (ndata) {	/* 处理段中的所有表 */
		if (ndata < 17) return JDR_FMT1;	/* 错误: 数据大小错误 */
		ndata -= 17;
		d = *data++;						/* 获取表编号和类别 */
		if (d & 0xEE) return JDR_FMT1;		/* 错误: 无效的类别/编号 */
		cls = d >> 4; num = d & 0x0F;		/* 类别 = dc(0)/ac(1), 表编号 = 0/1 */
		pb = alloc_pool(jd, 16);			/* 为位分布表分配内存块 */
		if (!pb) return JDR_MEM1;			/* 错误: 内存不足 */
		jd->huffbits[num][cls] = pb;
		for (np = i = 0; i < 16; i++) {		/* 加载 1 到 16 位码的模式数量 */
			np += (pb[i] = *data++);		/* 获取每个码的码字总数 */
		}
		ph = alloc_pool(jd, np * sizeof (uint16_t));/* 为码字表分配内存块 */
		if (!ph) return JDR_MEM1;			/* 错误: 内存不足 */
		jd->huffcode[num][cls] = ph;
		hc = 0;
		for (j = i = 0; i < 16; i++) {		/* 重建霍夫曼码字表 */
			b = pb[i];
			while (b--) ph[j++] = hc++;
			hc <<= 1;
		}

		if (ndata < np) return JDR_FMT1;	/* 错误: 数据大小错误 */
		ndata -= np;
		pd = alloc_pool(jd, np);			/* 为解码数据分配内存块 */
		if (!pd) return JDR_MEM1;			/* 错误: 内存不足 */
		jd->huffdata[num][cls] = pd;
		for (i = 0; i < np; i++) {			/* 加载与每个码字对应的解码数据 */
			d = *data++;
			if (!cls && d > 11) return JDR_FMT1;
			pd[i] = d;
		}
#if JD_FASTDECODE == 2
		{	/* 创建快速霍夫曼解码表 */
			unsigned int span, td, ti;
			uint16_t *tbl_ac = 0;
			uint8_t *tbl_dc = 0;

			if (cls) {
				tbl_ac = alloc_pool(jd, HUFF_LEN * sizeof (uint16_t));	/* AC 元素的查找表 */
				if (!tbl_ac) return JDR_MEM1;		/* 错误: 内存不足 */
				jd->hufflut_ac[num] = tbl_ac;
				memset(tbl_ac, 0xFF, HUFF_LEN * sizeof (uint16_t));		/* 默认值 (0xFFFF: 可能是长码) */
			} else {
				tbl_dc = alloc_pool(jd, HUFF_LEN * sizeof (uint8_t));	/* DC 元素的查找表 */
				if (!tbl_dc) return JDR_MEM1;		/* 错误: 内存不足 */
				jd->hufflut_dc[num] = tbl_dc;
				memset(tbl_dc, 0xFF, HUFF_LEN * sizeof (uint8_t));		/* 默认值 (0xFF: 可能是长码) */
			}
			for (i = b = 0; b < HUFF_BIT; b++) {	/* 创建查找表 */
				for (j = pb[b]; j; j--) {
					ti = ph[i] << (HUFF_BIT - 1 - b) & HUFF_MASK;	/* 码的输入模式索引 */
					if (cls) {
						td = pd[i++] | ((b + 1) << 8);	/* b15..b8: 码长度, b7..b0: 零游程和数据长度 */
						for (span = 1 << (HUFF_BIT - 1 - b); span; span--, tbl_ac[ti++] = (uint16_t)td) ;
					} else {
						td = pd[i++] | ((b + 1) << 4);	/* b7..b4: 码长度, b3..b0: 数据长度 */
						for (span = 1 << (HUFF_BIT - 1 - b); span; span--, tbl_dc[ti++] = (uint8_t)td) ;
					}
				}
			}
			jd->longofs[num][cls] = i;	/* 长码的码表偏移 */
		}
#endif
	}

	return JDR_OK;
}




/*-----------------------------------------------------------------------*/
/* 从输入流提取霍夫曼解码数据                                             */
/*-----------------------------------------------------------------------*/

static int huffext (	/* >=0: 解码数据, <0: 错误码 */
	JDEC* jd,			/* 解码器对象指针 */
	unsigned int id,	/* 表 ID (0:Y, 1:C) */
	unsigned int cls	/* 表类别 (0:DC, 1:AC) */
)
{
	size_t dc = jd->dctr;
	uint8_t *dp = jd->dptr;
	unsigned int d, flg = 0;

#if JD_FASTDECODE == 0
	uint8_t bm, nd, bl;
	const uint8_t *hb = jd->huffbits[id][cls];	/* 位分布表 */
	const uint16_t *hc = jd->huffcode[id][cls];	/* 码字表 */
	const uint8_t *hd = jd->huffdata[id][cls];	/* 数据表 */


	bm = jd->dbit;	/* 提取用的位掩码 */
	d = 0; bl = 16;	/* 最大码长度 */
	do {
		if (!bm) {		/* 下一个字节? */
			if (!dc) {	/* 无可用输入数据，重新填充输入缓冲区 */
				dp = jd->inbuf;	/* 输入缓冲区顶部 */
				dc = jd->infunc(jd, dp, JD_SZBUF);
				if (!dc) return 0 - (int)JDR_INP;	/* 错误: 读取错误或流异常终止 */
			} else {
				dp++;	/* 下一个数据指针 */
			}
			dc--;		/* 减少可用字节数 */
			if (flg) {		/* 在标志序列中? */
				flg = 0;	/* 退出标志序列 */
				if (*dp != 0) return 0 - (int)JDR_FMT1;	/* 错误: 检测到意外标志 (可能是损坏的数据) */
				*dp = 0xFF;				/* 该标志是数据 0xFF */
			} else {
				if (*dp == 0xFF) {		/* 是标志序列的开始? */
					flg = 1; continue;	/* 进入标志序列，获取后续字节 */
				}
			}
			bm = 0x80;		/* 从 MSB 读取 */
		}
		d <<= 1;			/* 获取一位 */
		if (*dp & bm) d++;
		bm >>= 1;

		for (nd = *hb++; nd; nd--) {	/* 在此位长度中搜索码字 */
			if (d == *hc++) {	/* 匹配? */
				jd->dbit = bm; jd->dctr = dc; jd->dptr = dp;
				return *hd;		/* 返回解码数据 */
			}
			hd++;
		}
		bl--;
	} while (bl);

#else
	const uint8_t *hb, *hd;
	const uint16_t *hc;
	unsigned int nc, bl, wbit = jd->dbit % 32;
	uint32_t w = jd->wreg & ((1UL << wbit) - 1);


	while (wbit < 16) {	/* 准备 16 位到工作寄存器 */
		if (jd->marker) {
			d = 0xFF;	/* 输入流因标记而停滞。生成填充位 */
		} else {
			if (!dc) {	/* 缓冲区空，重新填充输入缓冲区 */
				dp = jd->inbuf;						/* 输入缓冲区顶部 */
				dc = jd->infunc(jd, dp, JD_SZBUF);
				if (!dc) return 0 - (int)JDR_INP;	/* 错误: 读取错误或流异常终止 */
			}
			d = *dp++; dc--;
			if (flg) {		/* 在标志序列中? */
				flg = 0;	/* 退出标志序列 */
				if (d != 0) jd->marker = d;	/* 不是 0xFF 的转义而是标记 */
				d = 0xFF;
			} else {
				if (d == 0xFF) {		/* 是标志序列的开始? */
					flg = 1; continue;	/* 进入标志序列，获取后续字节 */
				}
			}
		}
		w = w << 8 | d;	/* 将 8 位移入工作寄存器 */
		wbit += 8;
	}
	jd->dctr = dc; jd->dptr = dp;
	jd->wreg = w;

#if JD_FASTDECODE == 2
	/* 短码的表搜索 */
	d = (unsigned int)(w >> (wbit - HUFF_BIT));	/* 短码作为表索引 */
	if (cls) {	/* AC 元素 */
		d = jd->hufflut_ac[id][d];	/* 表解码 */
		if (d != 0xFFFF) {	/* 如果命中短码则完成 */
			jd->dbit = wbit - (d >> 8);	/* 截取码长度 */
			return d & 0xFF;	/* b7..0: 零游程和后续数据位 */
		}
	} else {	/* DC 元素 */
		d = jd->hufflut_dc[id][d];	/* 表解码 */
		if (d != 0xFF) {	/* 如果命中短码则完成 */
			jd->dbit = wbit - (d >> 4);	/* 截取码长度 */
			return d & 0xF;	/* b3..0: 后续数据位 */
		}
	}

	/* 对长于 HUFF_BIT 的码进行增量搜索 */
	hb = jd->huffbits[id][cls] + HUFF_BIT;				/* 位分布表 */
	hc = jd->huffcode[id][cls] + jd->longofs[id][cls];	/* 码字表 */
	hd = jd->huffdata[id][cls] + jd->longofs[id][cls];	/* 数据表 */
	bl = HUFF_BIT + 1;
#else
	/* 对所有码进行增量搜索 */
	hb = jd->huffbits[id][cls];	/* 位分布表 */
	hc = jd->huffcode[id][cls];	/* 码字表 */
	hd = jd->huffdata[id][cls];	/* 数据表 */
	bl = 1;
#endif
	for ( ; bl <= 16; bl++) {	/* 增量搜索 */
		nc = *hb++;
		if (nc) {
			d = w >> (wbit - bl);
			do {	/* 在此位长度中搜索码字 */
				if (d == *hc++) {		/* 匹配? */
					jd->dbit = wbit - bl;	/* 截取霍夫曼码 */
					return *hd;			/* 返回解码数据 */
				}
				hd++;
			} while (--nc);
		}
	}
#endif

	return 0 - (int)JDR_FMT1;	/* 错误: 未找到码 (可能是损坏的数据) */
}




/*-----------------------------------------------------------------------*/
/* 从输入流提取 N 位                                                      */
/*-----------------------------------------------------------------------*/

static int bitext (	/* >=0: 提取的数据, <0: 错误码 */
	JDEC* jd,			/* 解码器对象指针 */
	unsigned int nbit	/* 要提取的位数 (1 到 16) */
)
{
	size_t dc = jd->dctr;
	uint8_t *dp = jd->dptr;
	unsigned int d, flg = 0;

#if JD_FASTDECODE == 0
	uint8_t mbit = jd->dbit;

	d = 0;
	do {
		if (!mbit) {			/* 下一个字节? */
			if (!dc) {			/* 无可用输入数据，重新填充输入缓冲区 */
				dp = jd->inbuf;	/* 输入缓冲区顶部 */
				dc = jd->infunc(jd, dp, JD_SZBUF);
				if (!dc) return 0 - (int)JDR_INP;	/* 错误: 读取错误或流异常终止 */
			} else {
				dp++;			/* 下一个数据指针 */
			}
			dc--;				/* 减少可用字节数 */
			if (flg) {			/* 在标志序列中? */
				flg = 0;		/* 退出标志序列 */
				if (*dp != 0) return 0 - (int)JDR_FMT1;	/* 错误: 检测到意外标志 (可能是损坏的数据) */
				*dp = 0xFF;		/* 该标志是数据 0xFF */
			} else {
				if (*dp == 0xFF) {		/* 是标志序列的开始? */
					flg = 1; continue;	/* 进入标志序列 */
				}
			}
			mbit = 0x80;		/* 从 MSB 读取 */
		}
		d <<= 1;	/* 获取一位 */
		if (*dp & mbit) d |= 1;
		mbit >>= 1;
		nbit--;
	} while (nbit);

	jd->dbit = mbit; jd->dctr = dc; jd->dptr = dp;
	return (int)d;

#else
	unsigned int wbit = jd->dbit % 32;
	uint32_t w = jd->wreg & ((1UL << wbit) - 1);


	while (wbit < nbit) {	/* 准备 nbit 位到工作寄存器 */
		if (jd->marker) {
			d = 0xFF;	/* 输入流停滞，生成填充位 */
		} else {
			if (!dc) {	/* 缓冲区空，重新填充输入缓冲区 */
				dp = jd->inbuf;	/* 输入缓冲区顶部 */
				dc = jd->infunc(jd, dp, JD_SZBUF);
				if (!dc) return 0 - (int)JDR_INP;	/* 错误: 读取错误或流异常终止 */
			}
			d = *dp++; dc--;
			if (flg) {		/* 在标志序列中? */
				flg = 0;	/* 退出标志序列 */
				if (d != 0) jd->marker = d;	/* 不是 0xFF 的转义而是标记 */
				d = 0xFF;
			} else {
				if (d == 0xFF) {		/* 是标志序列的开始? */
					flg = 1; continue;	/* 进入标志序列，获取后续字节 */
				}
			}
		}
		w = w << 8 | d;	/* 将 8 位移入工作寄存器 */
		wbit += 8;
	}
	jd->wreg = w; jd->dbit = wbit - nbit;
	jd->dctr = dc; jd->dptr = dp;

	return (int)(w >> ((wbit - nbit) % 32));
#endif
}




/*-----------------------------------------------------------------------*/
/* 处理重启间隔                                                           */
/*-----------------------------------------------------------------------*/

static JRESULT restart (
	JDEC* jd,		/* 解码器对象指针 */
	uint16_t rstn	/* 期望的重启序列号 */
)
{
	unsigned int i;
	uint8_t *dp = jd->dptr;
	size_t dc = jd->dctr;

#if JD_FASTDECODE == 0
	uint16_t d = 0;

	/* 从输入流获取两个字节 */
	for (i = 0; i < 2; i++) {
		if (!dc) {	/* 无可用输入数据，重新填充输入缓冲区 */
			dp = jd->inbuf;
			dc = jd->infunc(jd, dp, JD_SZBUF);
			if (!dc) return JDR_INP;
		} else {
			dp++;
		}
		dc--;
		d = d << 8 | *dp;	/* 获取一个字节 */
	}
	jd->dptr = dp; jd->dctr = dc; jd->dbit = 0;

	/* 检查标记 */
	if ((d & 0xFFD8) != 0xFFD0 || (d & 7) != (rstn & 7)) {
		return JDR_FMT1;	/* 错误: 未检测到期望的 RSTn 标记 (可能是损坏的数据) */
	}

#else
	uint16_t marker;


	if (jd->marker) {	/* 如果已检测到标记则生成 */
		marker = 0xFF00 | jd->marker;
		jd->marker = 0;
	} else {
		marker = 0;
		for (i = 0; i < 2; i++) {	/* 获取重启标记 */
			if (!dc) {		/* 无可用输入数据，重新填充输入缓冲区 */
				dp = jd->inbuf;
				dc = jd->infunc(jd, dp, JD_SZBUF);
				if (!dc) return JDR_INP;
			}
			marker = (marker << 8) | *dp++;	/* 获取一个字节 */
			dc--;
		}
		jd->dptr = dp; jd->dctr = dc;
	}

	/* 检查标记 */
	if ((marker & 0xFFD8) != 0xFFD0 || (marker & 7) != (rstn & 7)) {
		return JDR_FMT1;	/* 错误: 未检测到期望的 RSTn 标记 (可能是损坏的数据) */
	}

	jd->dbit = 0;			/* 丢弃填充位 */
#endif

	jd->dcv[2] = jd->dcv[1] = jd->dcv[0] = 0;	/* 重置 DC 偏移 */
	return JDR_OK;
}




/*-----------------------------------------------------------------------*/
/* 应用 Arai 算法的逆 DCT (另见 aa_idct.png)                              */
/*-----------------------------------------------------------------------*/

static void block_idct (
	int32_t* src,	/* 输入块数据 (已反量化并为 Arai 算法预缩放) */
	jd_yuv_t* dst	/* 存储块的目标指针 (字节数组) */
)
{
	const int32_t M13 = (int32_t)(1.41421*4096), M2 = (int32_t)(1.08239*4096), M4 = (int32_t)(2.61313*4096), M5 = (int32_t)(1.84776*4096);
	int32_t v0, v1, v2, v3, v4, v5, v6, v7;
	int32_t t10, t11, t12, t13;
	int i;

	/* 处理列 */
	for (i = 0; i < 8; i++) {
		v0 = src[8 * 0];	/* 获取偶数元素 */
		v1 = src[8 * 2];
		v2 = src[8 * 4];
		v3 = src[8 * 6];

		t10 = v0 + v2;		/* 处理偶数元素 */
		t12 = v0 - v2;
		t11 = (v1 - v3) * M13 >> 12;
		v3 += v1;
		t11 -= v3;
		v0 = t10 + v3;
		v3 = t10 - v3;
		v1 = t11 + t12;
		v2 = t12 - t11;

		v4 = src[8 * 7];	/* 获取奇数元素 */
		v5 = src[8 * 1];
		v6 = src[8 * 5];
		v7 = src[8 * 3];

		t10 = v5 - v4;		/* 处理奇数元素 */
		t11 = v5 + v4;
		t12 = v6 - v7;
		v7 += v6;
		v5 = (t11 - v7) * M13 >> 12;
		v7 += t11;
		t13 = (t10 + t12) * M5 >> 12;
		v4 = t13 - (t10 * M2 >> 12);
		v6 = t13 - (t12 * M4 >> 12) - v7;
		v5 -= v6;
		v4 -= v5;

		src[8 * 0] = v0 + v7;	/* 写回变换后的值 */
		src[8 * 7] = v0 - v7;
		src[8 * 1] = v1 + v6;
		src[8 * 6] = v1 - v6;
		src[8 * 2] = v2 + v5;
		src[8 * 5] = v2 - v5;
		src[8 * 3] = v3 + v4;
		src[8 * 4] = v3 - v4;

		src++;	/* 下一列 */
	}

	/* 处理行 */
	src -= 8;
	for (i = 0; i < 8; i++) {
		v0 = src[0] + (128L << 8);	/* 获取偶数元素 (在此移除 DC 偏移 (-128)) */
		v1 = src[2];
		v2 = src[4];
		v3 = src[6];

		t10 = v0 + v2;				/* 处理偶数元素 */
		t12 = v0 - v2;
		t11 = (v1 - v3) * M13 >> 12;
		v3 += v1;
		t11 -= v3;
		v0 = t10 + v3;
		v3 = t10 - v3;
		v1 = t11 + t12;
		v2 = t12 - t11;

		v4 = src[7];				/* 获取奇数元素 */
		v5 = src[1];
		v6 = src[5];
		v7 = src[3];

		t10 = v5 - v4;				/* 处理奇数元素 */
		t11 = v5 + v4;
		t12 = v6 - v7;
		v7 += v6;
		v5 = (t11 - v7) * M13 >> 12;
		v7 += t11;
		t13 = (t10 + t12) * M5 >> 12;
		v4 = t13 - (t10 * M2 >> 12);
		v6 = t13 - (t12 * M4 >> 12) - v7;
		v5 -= v6;
		v4 -= v5;

		/* 将变换后的值缩小 8 位并输出一行 */
#if JD_FASTDECODE >= 1
		dst[0] = (int16_t)((v0 + v7) >> 8);
		dst[7] = (int16_t)((v0 - v7) >> 8);
		dst[1] = (int16_t)((v1 + v6) >> 8);
		dst[6] = (int16_t)((v1 - v6) >> 8);
		dst[2] = (int16_t)((v2 + v5) >> 8);
		dst[5] = (int16_t)((v2 - v5) >> 8);
		dst[3] = (int16_t)((v3 + v4) >> 8);
		dst[4] = (int16_t)((v3 - v4) >> 8);
#else
		dst[0] = BYTECLIP((v0 + v7) >> 8);
		dst[7] = BYTECLIP((v0 - v7) >> 8);
		dst[1] = BYTECLIP((v1 + v6) >> 8);
		dst[6] = BYTECLIP((v1 - v6) >> 8);
		dst[2] = BYTECLIP((v2 + v5) >> 8);
		dst[5] = BYTECLIP((v2 - v5) >> 8);
		dst[3] = BYTECLIP((v3 + v4) >> 8);
		dst[4] = BYTECLIP((v3 - v4) >> 8);
#endif

		dst += 8; src += 8;	/* 下一行 */
	}
}




/*-----------------------------------------------------------------------*/
/* 将 MCU 中的所有块加载到工作缓冲区                                       */
/*-----------------------------------------------------------------------*/

static JRESULT mcu_load (
	JDEC* jd		/* 解码器对象指针 */
)
{
	int32_t *tmp = (int32_t*)jd->workbuf;	/* 用于反量化和 IDCT 的块工作缓冲区 */
	int d, e;
	unsigned int blk, nby, i, bc, z, id, cmp;
	jd_yuv_t *bp;
	const int32_t *dqf;


	nby = jd->msx * jd->msy;	/* Y 块数量 (1, 2 或 4) */
	bp = jd->mcubuf;			/* 指向 MCU 第一个块的指针 */

	for (blk = 0; blk < nby + 2; blk++) {	/* 获取 nby 个 Y 块和两个 C 块 */
		cmp = (blk < nby) ? 0 : blk - nby + 1;	/* 分量编号 0:Y, 1:Cb, 2:Cr */

		if (cmp && jd->ncomp != 3) {		/* 如果不存在则清除 C 块 (单色图像) */
			for (i = 0; i < 64; bp[i++] = 128) ;

		} else {							/* 从输入流加载 Y/C 块 */
			id = cmp ? 1 : 0;						/* 此分量的霍夫曼表 ID */

			/* 从输入流提取 DC 元素 */
			d = huffext(jd, id, 0);					/* 提取霍夫曼编码数据 (位长度) */
			if (d < 0) return (JRESULT)(0 - d);		/* 错误: 无效码或输入错误 */
			bc = (unsigned int)d;
			d = jd->dcv[cmp];						/* 前一个块的 DC 值 */
			if (bc) {								/* 如果与前一个块有差异 */
				e = bitext(jd, bc);					/* 提取数据位 */
				if (e < 0) return (JRESULT)(0 - e);	/* 错误: 输入错误 */
				bc = 1 << (bc - 1);					/* MSB 位置 */
				if (!(e & bc)) e -= (bc << 1) - 1;	/* 如需要则恢复负值 */
				d += e;								/* 获取当前值 */
				jd->dcv[cmp] = (int16_t)d;			/* 保存当前 DC 值供下一个块使用 */
			}
			dqf = jd->qttbl[jd->qtid[cmp]];			/* 此分量的反量化表 ID */
			tmp[0] = d * dqf[0] >> 8;				/* 反量化，应用 Arai 算法缩放因子并缩小 8 位 */

			/* 从输入流提取后续 63 个 AC 元素 */
			memset(&tmp[1], 0, 63 * sizeof (int32_t));	/* 初始化所有 AC 元素 */
			z = 1;		/* AC 元素顶部 (Z字形顺序) */
			do {
				d = huffext(jd, id, 1);				/* 提取霍夫曼编码值 (零游程和位长度) */
				if (d == 0) break;					/* EOB? */
				if (d < 0) return (JRESULT)(0 - d);	/* 错误: 无效码或输入错误 */
				bc = (unsigned int)d;
				z += bc >> 4;						/* 跳过前导零游程 */
				if (z >= 64) return JDR_FMT1;		/* 零游程过长 */
				if (bc &= 0x0F) {					/* 位长度? */
					d = bitext(jd, bc);				/* 提取数据位 */
					if (d < 0) return (JRESULT)(0 - d);	/* 错误: 输入设备错误 */
					bc = 1 << (bc - 1);				/* MSB 位置 */
					if (!(d & bc)) d -= (bc << 1) - 1;	/* 如需要则恢复负值 */
					i = Zig[z];						/* 获取光栅顺序索引 */
					tmp[i] = d * dqf[i] >> 8;		/* 反量化，应用 Arai 算法缩放因子并缩小 8 位 */
				}
			} while (++z < 64);		/* 下一个 AC 元素 */

			if (JD_FORMAT != 2 || !cmp) {	/* 如果是灰度输出则可能不处理 C 分量 */
				if (z == 1 || (JD_USE_SCALE && jd->scale == 3)) {	/* 如果没有 AC 元素或缩放比为 1/8，可省略 IDCT，用 DC 值填充块 */
					d = (jd_yuv_t)((*tmp / 256) + 128);
					if (JD_FASTDECODE >= 1) {
						for (i = 0; i < 64; bp[i++] = d) ;
					} else {
						memset(bp, d, 64);
					}
				} else {
					block_idct(tmp, bp);	/* 应用 IDCT 并将块存储到 MCU 缓冲区 */
				}
			}
		}

		bp += 64;				/* 下一个块 */
	}

	return JDR_OK;	/* 所有块已成功加载 */
}




/*-----------------------------------------------------------------------*/
/* 输出 MCU: 将 YCrCb 转换为 RGB 并以 RGB 格式输出                         */
/*-----------------------------------------------------------------------*/

static JRESULT mcu_output (
	JDEC* jd,			/* 解码器对象指针 */
	int (*outfunc)(JDEC*, void*, JRECT*),	/* RGB 输出函数 */
	unsigned int x,		/* 图像中的 MCU 位置 */
	unsigned int y		/* 图像中的 MCU 位置 */
)
{
	const int CVACC = (sizeof (int) > 2) ? 1024 : 128;	/* 16/32 位系统的自适应精度 */
	unsigned int ix, iy, mx, my, rx, ry;
	int yy, cb, cr;
	jd_yuv_t *py, *pc;
	uint8_t *pix;
	JRECT rect;


	mx = jd->msx * 8; my = jd->msy * 8;					/* MCU 大小 (像素) */
	rx = (x + mx <= jd->width) ? mx : jd->width - x;	/* 输出矩形大小 (可能在图像右/下边缘被裁剪) */
	ry = (y + my <= jd->height) ? my : jd->height - y;
	if (JD_USE_SCALE) {
		rx >>= jd->scale; ry >>= jd->scale;
		if (!rx || !ry) return JDR_OK;					/* 如果所有像素都被舍入则跳过此 MCU */
		x >>= jd->scale; y >>= jd->scale;
	}
	rect.left = x; rect.right = x + rx - 1;				/* 帧缓冲区中的矩形区域 */
	rect.top = y; rect.bottom = y + ry - 1;


	if (!JD_USE_SCALE || jd->scale != 3) {	/* 非 1/8 缩放 */
		pix = (uint8_t*)jd->workbuf;

		if (JD_FORMAT != 2) {	/* RGB 输出 (从 Y/C 分量构建 RGB MCU) */
			for (iy = 0; iy < my; iy++) {
				pc = py = jd->mcubuf;
				if (my == 16) {		/* 双倍块高度? */
					pc += 64 * 4 + (iy >> 1) * 8;
					if (iy >= 8) py += 64;
				} else {			/* 单倍块高度 */
					pc += mx * 8 + iy * 8;
				}
				py += iy * 8;
				for (ix = 0; ix < mx; ix++) {
					cb = pc[0] - 128; 	/* 获取 Cb/Cr 分量并移除偏移 */
					cr = pc[64] - 128;
					if (mx == 16) {					/* 双倍块宽度? */
						if (ix == 8) py += 64 - 8;	/* 如果是双倍块高度则跳到下一个块 */
						pc += ix & 1;				/* 每两个像素前进一次色度指针 */
					} else {						/* 单倍块宽度 */
						pc++;						/* 每个像素前进一次色度指针 */
					}
					yy = *py++;			/* 获取 Y 分量 */
					*pix++ = /*R*/ BYTECLIP(yy + ((int)(1.402 * CVACC) * cr) / CVACC);
					*pix++ = /*G*/ BYTECLIP(yy - ((int)(0.344 * CVACC) * cb + (int)(0.714 * CVACC) * cr) / CVACC);
					*pix++ = /*B*/ BYTECLIP(yy + ((int)(1.772 * CVACC) * cb) / CVACC);
				}
			}
		} else {	/* 单色输出 (从 Y 分量构建灰度 MCU) */
			for (iy = 0; iy < my; iy++) {
				py = jd->mcubuf + iy * 8;
				if (my == 16) {		/* 双倍块高度? */
					if (iy >= 8) py += 64;
				}
				for (ix = 0; ix < mx; ix++) {
					if (mx == 16) {					/* 双倍块宽度? */
						if (ix == 8) py += 64 - 8;	/* 如果是双倍块高度则跳到下一个块 */
					}
					*pix++ = (uint8_t)*py++;			/* 获取并存储 Y 值作为灰度 */
				}
			}
		}

		/* 如需要则缩小 MCU 矩形 */
		if (JD_USE_SCALE && jd->scale) {
			unsigned int x, y, r, g, b, s, w, a;
			uint8_t *op;

			/* 获取对应每个像素的方块的平均 RGB 值 */
			s = jd->scale * 2;	/* 平均用的移位数 */
			w = 1 << jd->scale;	/* 方块宽度 */
			a = (mx - w) * (JD_FORMAT != 2 ? 3 : 1);	/* 方块中下一行要跳过的字节数 */
			op = (uint8_t*)jd->workbuf;
			for (iy = 0; iy < my; iy += w) {
				for (ix = 0; ix < mx; ix += w) {
					pix = (uint8_t*)jd->workbuf + (iy * mx + ix) * (JD_FORMAT != 2 ? 3 : 1);
					r = g = b = 0;
					for (y = 0; y < w; y++) {	/* 累加方块中的 RGB 值 */
						for (x = 0; x < w; x++) {
							r += *pix++;	/* 累加 R 或 Y (单色输出) */
							if (JD_FORMAT != 2) {	/* RGB 输出? */
								g += *pix++;	/* 累加 G */
								b += *pix++;	/* 累加 B */
							}
						}
						pix += a;
					}							/* 放入平均像素值 */
					*op++ = (uint8_t)(r >> s);	/* 放入 R 或 Y (单色输出) */
					if (JD_FORMAT != 2) {	/* RGB 输出? */
						*op++ = (uint8_t)(g >> s);	/* 放入 G */
						*op++ = (uint8_t)(b >> s);	/* 放入 B */
					}
				}
			}
		}

	} else {	/* 仅用于 1/8 缩放 (每个块中左上角像素是该块的 DC 值) */

		/* 从离散分量构建 1/8 缩小的 RGB MCU */
		pix = (uint8_t*)jd->workbuf;
		pc = jd->mcubuf + mx * my;
		cb = pc[0] - 128;		/* 获取 Cb/Cr 分量并恢复正确电平 */
		cr = pc[64] - 128;
		for (iy = 0; iy < my; iy += 8) {
			py = jd->mcubuf;
			if (iy == 8) py += 64 * 2;
			for (ix = 0; ix < mx; ix += 8) {
				yy = *py;	/* 获取 Y 分量 */
				py += 64;
				if (JD_FORMAT != 2) {
					*pix++ = /*R*/ BYTECLIP(yy + ((int)(1.402 * CVACC) * cr / CVACC));
					*pix++ = /*G*/ BYTECLIP(yy - ((int)(0.344 * CVACC) * cb + (int)(0.714 * CVACC) * cr) / CVACC);
					*pix++ = /*B*/ BYTECLIP(yy + ((int)(1.772 * CVACC) * cb / CVACC));
				} else {
					*pix++ = yy;
				}
			}
		}
	}

	/* 如果 MCU 的一部分要被截断则压缩像素表 */
	mx >>= jd->scale;
	if (rx < mx) {	/* MCU 跨越右边缘? */
		uint8_t *s, *d;
		unsigned int x, y;

		s = d = (uint8_t*)jd->workbuf;
		for (y = 0; y < ry; y++) {
			for (x = 0; x < rx; x++) {	/* 复制有效像素 */
				*d++ = *s++;
				if (JD_FORMAT != 2) {
					*d++ = *s++;
					*d++ = *s++;
				}
			}
			s += (mx - rx) * (JD_FORMAT != 2 ? 3 : 1);	/* 跳过截断的像素 */
		}
	}

	/* 如需要将 RGB888 转换为 RGB565 */
	if (JD_FORMAT == 1) {
		uint8_t *s = (uint8_t*)jd->workbuf;
		uint16_t w, *d = (uint16_t*)s;
		unsigned int n = rx * ry;

		do {
			w = (*s++ & 0xF8) << 8;		/* RRRRR----------- */
			w |= (*s++ & 0xFC) << 3;	/* -----GGGGGG----- */
			w |= *s++ >> 3;				/* -----------BBBBB */
			*d++ = w;
		} while (--n);
	}

	/* 输出矩形 */
	return outfunc(jd, jd->workbuf, &rect) ? JDR_OK : JDR_INTR; 
}




/*-----------------------------------------------------------------------*/
/* 分析 JPEG 图像并初始化解码器对象                                        */
/*-----------------------------------------------------------------------*/

#define	LDB_WORD(ptr)		(uint16_t)(((uint16_t)*((uint8_t*)(ptr))<<8)|(uint16_t)*(uint8_t*)((ptr)+1))


JRESULT jd_prepare (
	JDEC* jd,				/* 空白解码器对象 */
	size_t (*infunc)(JDEC*, uint8_t*, size_t),	/* JPEG 流输入函数 */
	void* pool,				/* 解压会话的工作缓冲区 */
	size_t sz_pool,			/* 工作缓冲区大小 */
	void* dev				/* 本次会话的 I/O 设备标识符 */
)
{
	uint8_t *seg, b;
	uint16_t marker;
	unsigned int n, i, ofs;
	size_t len;
	JRESULT rc;


	memset(jd, 0, sizeof (JDEC));	/* 清除解码器对象 (如果机器的空指针不是全零位可能会有问题) */
	jd->pool = pool;		/* 工作内存 */
	jd->sz_pool = sz_pool;	/* 给定工作内存的大小 */
	jd->infunc = infunc;	/* 流输入函数 */
	jd->device = dev;		/* I/O 设备标识符 */

	jd->inbuf = seg = alloc_pool(jd, JD_SZBUF);		/* 分配流输入缓冲区 */
	if (!seg) return JDR_MEM1;

	ofs = marker = 0;		/* 查找 SOI 标记 */
	do {
		if (jd->infunc(jd, seg, 1) != 1) return JDR_INP;	/* 错误: 未检测到 SOI */
		ofs++;
		marker = marker << 8 | seg[0];
	} while (marker != 0xFFD8);

	for (;;) {				/* 解析 JPEG 段 */
		/* 获取 JPEG 标记 */
		if (jd->infunc(jd, seg, 4) != 4) return JDR_INP;
		marker = LDB_WORD(seg);		/* 标记 */
		len = LDB_WORD(seg + 2);	/* 长度字段 */
		if (len <= 2 || (marker >> 8) != 0xFF) return JDR_FMT1;
		len -= 2;			/* 段内容大小 */
		ofs += 4 + len;		/* 已加载的字节数 */

		switch (marker & 0xFF) {
		case 0xC0:	/* SOF0 (基线 JPEG) */
			if (len > JD_SZBUF) return JDR_MEM2;
			if (jd->infunc(jd, seg, len) != len) return JDR_INP;	/* 加载段数据 */

			jd->width = LDB_WORD(&seg[3]);		/* 图像宽度 (像素) */
			jd->height = LDB_WORD(&seg[1]);		/* 图像高度 (像素) */
			jd->ncomp = seg[5];					/* 颜色分量数 */
			if (jd->ncomp != 3 && jd->ncomp != 1) return JDR_FMT3;	/* 错误: 仅支持灰度和 Y/Cb/Cr */

			/* 检查每个图像分量 */
			for (i = 0; i < jd->ncomp; i++) {
				b = seg[7 + 3 * i];							/* 获取采样因子 */
				if (i == 0) {	/* Y 分量 */
					if (b != 0x11 && b != 0x22 && b != 0x21) {	/* 检查采样因子 */
						return JDR_FMT3;					/* 错误: 仅支持 4:4:4, 4:2:0 或 4:2:2 */
					}
					jd->msx = b >> 4; jd->msy = b & 15;		/* MCU 大小 [块] */
				} else {		/* Cb/Cr 分量 */
					if (b != 0x11) return JDR_FMT3;			/* 错误: Cb/Cr 的采样因子必须为 1 */
				}
				jd->qtid[i] = seg[8 + 3 * i];				/* 获取此分量的反量化表 ID */
				if (jd->qtid[i] > 3) return JDR_FMT3;		/* 错误: 无效 ID */
			}
			break;

		case 0xDD:	/* DRI - 定义重启间隔 */
			if (len > JD_SZBUF) return JDR_MEM2;
			if (jd->infunc(jd, seg, len) != len) return JDR_INP;	/* 加载段数据 */

			jd->nrst = LDB_WORD(seg);	/* 获取重启间隔 (MCU 数) */
			break;

		case 0xC4:	/* DHT - 定义霍夫曼表 */
			if (len > JD_SZBUF) return JDR_MEM2;
			if (jd->infunc(jd, seg, len) != len) return JDR_INP;	/* 加载段数据 */

			rc = create_huffman_tbl(jd, seg, len);	/* 创建霍夫曼表 */
			if (rc) return rc;
			break;

		case 0xDB:	/* DQT - 定义量化表 */
			if (len > JD_SZBUF) return JDR_MEM2;
			if (jd->infunc(jd, seg, len) != len) return JDR_INP;	/* 加载段数据 */

			rc = create_qt_tbl(jd, seg, len);	/* 创建反量化表 */
			if (rc) return rc;
			break;

		case 0xDA:	/* SOS - 扫描开始 */
			if (len > JD_SZBUF) return JDR_MEM2;
			if (jd->infunc(jd, seg, len) != len) return JDR_INP;	/* 加载段数据 */

			if (!jd->width || !jd->height) return JDR_FMT1;	/* 错误: 无效图像尺寸 */
			if (seg[0] != jd->ncomp) return JDR_FMT3;		/* 错误: 颜色分量数错误 */

			/* 检查对应每个分量的所有表是否已加载 */
			for (i = 0; i < jd->ncomp; i++) {
				b = seg[2 + 2 * i];	/* 获取霍夫曼表 ID */
				if (b != 0x00 && b != 0x11)	return JDR_FMT3;	/* 错误: DC/AC 元素的表编号不同 */
				n = i ? 1 : 0;							/* 分量类别 */
				if (!jd->huffbits[n][0] || !jd->huffbits[n][1]) {	/* 检查此分量的霍夫曼表 */
					return JDR_FMT1;					/* 错误: 未加载 */
				}
				if (!jd->qttbl[jd->qtid[i]]) {			/* 检查此分量的反量化表 */
					return JDR_FMT1;					/* 错误: 未加载 */
				}
			}

			/* 为 MCU 和像素输出分配工作缓冲区 */
			n = jd->msy * jd->msx;						/* MCU 中的 Y 块数 */
			if (!n) return JDR_FMT1;					/* 错误: SOF0 未加载 */
			len = n * 64 * 2 + 64;						/* 为 IDCT 和 RGB 输出分配缓冲区 */
			if (len < 256) len = 256;					/* 但 IDCT 至少需要 256 字节 */
			jd->workbuf = alloc_pool(jd, len);			/* 它可能占用后续 MCU 工作缓冲区的一部分用于 RGB 输出 */
			if (!jd->workbuf) return JDR_MEM1;			/* 错误: 内存不足 */
			jd->mcubuf = alloc_pool(jd, (n + 2) * 64 * sizeof (jd_yuv_t));	/* 分配 MCU 工作缓冲区 */
			if (!jd->mcubuf) return JDR_MEM1;			/* 错误: 内存不足 */

			/* 将流读取偏移对齐到 JD_SZBUF */
			if (ofs %= JD_SZBUF) {
				jd->dctr = jd->infunc(jd, seg + ofs, (size_t)(JD_SZBUF - ofs));
			}
			jd->dptr = seg + ofs - (JD_FASTDECODE ? 0 : 1);

			return JDR_OK;		/* 初始化成功。准备解压 JPEG 图像。 */

		case 0xC1:	/* SOF1 */
		case 0xC2:	/* SOF2 */
		case 0xC3:	/* SOF3 */
		case 0xC5:	/* SOF5 */
		case 0xC6:	/* SOF6 */
		case 0xC7:	/* SOF7 */
		case 0xC9:	/* SOF9 */
		case 0xCA:	/* SOF10 */
		case 0xCB:	/* SOF11 */
		case 0xCD:	/* SOF13 */
		case 0xCE:	/* SOF14 */
		case 0xCF:	/* SOF15 */
		case 0xD9:	/* EOI */
			return JDR_FMT3;	/* 不支持的 JPEG 标准 (可能是渐进式 JPEG) */

		default:	/* 未知段 (注释, exif 等) */
			/* 跳过段数据 (空指针表示从流中移除数据) */
			if (jd->infunc(jd, 0, len) != len) return JDR_INP;
		}
	}
}




/*-----------------------------------------------------------------------*/
/* 开始解压 JPEG 图像                                                      */
/*-----------------------------------------------------------------------*/

JRESULT jd_decomp (
	JDEC* jd,								/* 已初始化的解码器对象 */
	int (*outfunc)(JDEC*, void*, JRECT*),	/* RGB 输出函数 */
	uint8_t scale							/* 输出缩小因子 (0 到 3) */
)
{
	unsigned int x, y, mx, my;
	uint16_t rst, rsc;
	JRESULT rc;


	if (scale > (JD_USE_SCALE ? 3 : 0)) return JDR_PAR;
	jd->scale = scale;

	mx = jd->msx * 8; my = jd->msy * 8;			/* MCU 大小 (像素) */

	jd->dcv[2] = jd->dcv[1] = jd->dcv[0] = 0;	/* 初始化 DC 值 */
	rst = rsc = 0;

	rc = JDR_OK;
	for (y = 0; y < jd->height; y += my) {		/* MCU 垂直循环 */
		for (x = 0; x < jd->width; x += mx) {	/* MCU 水平循环 */
			if (jd->nrst && rst++ == jd->nrst) {	/* 如果启用则处理重启间隔 */
				rc = restart(jd, rsc++);
				if (rc != JDR_OK) return rc;
				rst = 1;
			}
			rc = mcu_load(jd);					/* 加载 MCU (解压霍夫曼编码流、反量化并应用 IDCT) */
			if (rc != JDR_OK) return rc;
			rc = mcu_output(jd, outfunc, x, y);	/* 输出 MCU (YCbCr 转 RGB、缩放并输出) */
			if (rc != JDR_OK) return rc;
		}
	}

	return rc;
}
