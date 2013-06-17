/************************************************************
 * Copyright (C), 2008-2012,
 * FileName:		// �ļ���
 * Author:			// ����
 * Date:			// ����
 * Description:		// ģ������
 * Version:			// �汾��Ϣ
 * Function List:	// ��Ҫ�������书��
 *     1. -------
 * History:			// ��ʷ�޸ļ�¼
 *     <author>  <time>   <version >   <desc>
 *     David    96/10/12     1.0     build this moudle
 ***********************************************************/
#include <stdio.h>
#include <rtthread.h>
#include <rtdevice.h>
#include <dfs_posix.h>

#include "stm32f4xx.h"
#include "rs485.h"
#include "jt808.h"
#include "camera.h"
#include <finsh.h>
#include "sst25.h"

#define JT808_PACKAGE_MAX		512

typedef enum 
{
	CAM_NONE=0,
	CAM_IDLE=1,
	CAM_START,
	CAM_GET_PHOTO,
	CAM_RX_PHOTO,
	CAM_END,
	CAM_FALSE
}CAM_STATE;


typedef enum
{
	RX_IDLE=0,
	RX_SYNC1,
	RX_SYNC2,
	RX_HEAD,
	RX_DATA,
	RX_FCS,
	RX_0D,
	RX_0A,
}CAM_RX_STATE;	


typedef  struct
{
	CAM_STATE	State;				///�������״̬��
	CAM_RX_STATE Rx_State;			///����״̬
	u8			Retry;				///�ظ��������
	Style_Cam_Requset_Para	Para;	///������ǰ���������Ϣ
} Style_Cam_Para;


typedef __packed struct
{
	char 	Head[6];				///�������֣���ʾ��ǰ��������Ϊĳ�̶����ݿ�ʼ
	u32		Len;					///���ݳ��ȣ���������ͷ��������,����ͷ���̶ֹ�Ϊ64�ֽ�
	u8		State;					///��ʾͼƬ״̬��ǣ�0xFFΪ��ʼ��״̬��bit0==0��ʾ�Ѿ�ɾ��,bit1==0��ʾ�ɹ��ϴ�,bit2==0��ʾ������Ϊ����������
	T_TIMES	Time;					///��¼���ݵ�ʱ�䣬BCD���ʾ��YY-MM-DD-hh-mm-ss
	u32		Data_ID;				///����ID,˳�������ʽ��¼
	u8		Media_Format;			///0��JPEG��1��TIF��2��MP3��3��WAV��4��WMV�� ��������
	u8		Media_Style;			///��������:0��ͼ��1����Ƶ��2����Ƶ��
	u8		Channel_ID;				///����ͨ��ID
	u8		TiggerStyle;			///������ʽ
	u8		position[28];			///λ����Ϣ
}TypeDF_PackageHead;

typedef __packed struct
{
	u32 	Address;				///��ַ
	u32 	Len;					///����
	u32		Data_ID;				///����ID
}TypeDF_PackageInfo;

typedef __packed struct
{
	u16		Number;					///ͼƬ����
	TypeDF_PackageInfo	FirstPic;	///��һ��ͼƬ
	TypeDF_PackageInfo	LastPic;	///���һ��ͼƬ
}TypeDF_PICPara;


typedef __packed struct
{
	u32 	Address;				///��ַ
	u32		Data_ID;				///����ID
	u8		Delete;					///ɾ�����
	u8		Pack_Mark[16];			///�����
}TypePicMultTransPara;


#define	DF_CamAddress_Start		0x1000			///ͼƬ���ݴ洢��ʼλ��
#define DF_CamAddress_End		0X20000			///ͼƬ���ݴ洢����λ��
#define DF_CamSaveSect			0x400			///ͼƬ���ݴ洢��С���

extern rt_device_t _console_device;
extern u16 Hex_To_Ascii(const u8* pSrc, u8* pDst, u16 nSrcLength);

const char  CAM_HEAD[]={"PIC_01"};

static Style_Cam_Para			Current_Cam_Para={CAM_NONE,0,0,0};		
static TypeDF_PICPara			DF_PicParameter;						///FLASH�洢��ͼƬ��Ϣ

/* ��Ϣ���п��ƿ�*/
struct rt_messagequeue mq_Cam;
/* ��Ϣ�������õ��ķ�����Ϣ���ڴ��*/
static char msgpool_cam[256];

static char CamTestBuf[2000];

extern MsgList* list_jt808_tx;



/*********************************************************************************
*��������:u8 HEX_to_BCD(u8 HEX)
*��������:��1���ֽڴ�С��HEX��ת��ΪBCD�룻����BCD��
*�� ��:none
*�� ��:none 
*�� �� ֵ:none
*�� ��:������
*��������:2013-06-3
*---------------------------------------------------------------------------------
*�� �� ��:
*�޸�����:
*�޸�����:
*********************************************************************************/
u8 HEX_to_BCD(u8 HEX)
{
 u8 BCD_code=0;
 BCD_code=HEX%10;
 BCD_code|=((HEX%100)/10)<<4;
 return BCD_code; 
}


/*********************************************************************************
*��������:u8 BCD_to_HEX(u8 BCD)
*��������:��1���ֽڴ�С��BCD��ת��ΪHEX�룻����HEX��
*�� ��:none
*�� ��:none 
*�� �� ֵ:none
*�� ��:������
*��������:2013-06-3
*---------------------------------------------------------------------------------
*�� �� ��:
*�޸�����:
*�޸�����:
*********************************************************************************/
u8 BCD_to_HEX(u8 BCD)
{
 u8 HEX_code=0;
 HEX_code = BCD&0x0F;
 HEX_code += (BCD>>4)*10;
 return HEX_code;
}



/*********************************************************************************
*��������:bool leap_year(u16 year)
*��������:������������year�ǲ�������,�����귵��1,���򷵻�0;
			����Ϊ366�����Ϊ365��,�����2��Ϊ29��.
*��	��:year	��
*��	��:none 
*�� �� ֵ:,�����귵��1,���򷵻�0;
*��	��:������
*��������:2013-06-3
*---------------------------------------------------------------------------------
*�� �� ��:
*�޸�����:
*�޸�����:
*********************************************************************************/
u8 leap_year(u16 year)
{
 u8 leap;
 if((year&0x0003)==0)
 	{
 	if(year%100==0)
 		{
		if(year%400==0)
			leap=1;
		else
			leap=0;
 		}
	else
		leap=1;
 	}
 else
 	leap=0;
 return leap;
}


/*********************************************************************************
*��������:u32 Timer_To_Day(T_TIMES *T)
*��������:�����ʱ�������������ʼʱ��Ϊ2000��1��1��
*��	��:none
*��	��:none 
*�� �� ֵ:ʱ���Ӧ����
*��	��:������
*��������:2013-06-3
*---------------------------------------------------------------------------------
*�� �� ��:
*�޸�����:
*�޸�����:
*********************************************************************************/
u32 Timer_To_Day(T_TIMES *T)
{
 u32 long_day;
 u16 i,year,month,day;
 
 year=BCD_to_HEX(T->years);
 month=BCD_to_HEX(T->months);
 day=BCD_to_HEX(T->days);
 year+=2000;
 long_day=0;
 for(i=2000;i<year;i++)
 	{
 	long_day+=365;
	long_day+=leap_year(i);	
 	}
 
 switch(month)
 	{
	case 12 :
 		long_day+=30;
	case 11 :
 		long_day+=31;
	case 10 :
 		long_day+=30;
	case 9 :
 		long_day+=31;
	case 8 :
 		long_day+=31;
	case 7 :
 		long_day+=30;
	case 6 :
 		long_day+=31;
	case 5 :
 		long_day+=30;
	case 4 :
 		long_day+=31;
	case 3 :
		{
 		long_day+=28;
		long_day+=leap_year(year);	
		}	
	case 2 :
 		long_day+=31;
	case 1 :
 		long_day+=day-1;
	default :
		nop;
 	}
 return long_day;
}


/*********************************************************************************
*��������:u8 Get_Month_Day(u8 month,u8 leapyear)
*��������:��ȡ���µ�����
*��	��:none
*��	��:none 
*�� �� ֵ:none
*��	��:������
*��������:2013-06-3
*---------------------------------------------------------------------------------
*�� �� ��:
*�޸�����:
*�޸�����:
*********************************************************************************/
u8 Get_Month_Day(u8 month,u8 leapyear)
{
  u8 day;
  switch(month)
 	{
	case 12 :
 		day=31;
		break;
	case 11 :
 		day=30;
		break;
	case 10 :
 		day=31;
		break;
	case 9 :
 		day=30;
		break;
	case 8 :
 		day=31;
		break;
	case 7 :
 		day=31;
		break;
	case 6 :
 		day=30;
		break;
	case 5 :
 		day=31;
		break;
	case 4 :
 		day=30;
		break;
	case 3 :
 		day=31;
		break;
	case 2 :
		{
 		day=28;
		day+=leapyear;	
		break;
		}	
	case 1 :
 		day=31;
		break;
	default :
		nop;
 	}
  return day;
}


/*********************************************************************************
*��������:u32 Times_To_LongInt(T_TIMES *T)
*��������:��������RTC_TIMES���͵�ʱ��ת��Ϊlong int���͵����ݣ�
		��λΪ�룬��ʼʱ��Ϊ2000��1��1��00:00:00
*��	��:none
*��	��:none 
*�� �� ֵ:none
*��	��:������
*��������:2013-06-3
*---------------------------------------------------------------------------------
*�� �� ��:
*�޸�����:
*�޸�����:
*********************************************************************************/
u32 Times_To_LongInt(T_TIMES *T)
{
 u32 timer_int,hour;
 u16 i,minute,second;
 hour=BCD_to_HEX(T->hours);
 minute=BCD_to_HEX(T->minutes);
 second=BCD_to_HEX(T->seconds);
 timer_int=Timer_To_Day(T);
 hour*=3600;
 minute*=60;
 timer_int=timer_int*86400+hour+minute+second;			///timer_int*24*3600=timer_int*86400
 return timer_int;
}



/*********************************************************************************
*��������:void LongInt_To_Times(u32 timer_int, T_TIMES *T)
*��������:����׼ʱ����2000��1��1��00:00:00�ĳ���������תΪT_TIMES����
*��	��:none
*��	��:none 
*�� �� ֵ:none
*��	��:������
*��������:2013-06-3
*---------------------------------------------------------------------------------
*�� �� ��:
*�޸�����:
*�޸�����:
**********************************************6***********************************/
void LongInt_To_Times(u32 timer_int, T_TIMES *T)
{
 u32 long_day1,long_day2;
 u16 i,day,leapyear;
 long_day2=0;
 long_day1=timer_int/86400;
 for(i=2000;i<2100;i++)
 	{
 	day=365+leap_year(i);
 	long_day2+=day;
	//�����ǰ���������С�ڼ���������õ������
	if(long_day2>long_day1)
		{
		long_day2-=day;
		leapyear=leap_year(i);
		break;
		}
 	}
 T->years=HEX_to_BCD(i-2000);
 for(i=1;i<=12;i++)
 	{
 	day=Get_Month_Day(i,leapyear);
 	long_day2+=day;
	//�����ǰ�µ�������С�ڼ���������õ����·�
	if(long_day2>long_day1)
		{
		long_day2-=day;
		break;
		}
 	}
 T->months=HEX_to_BCD(i);
 day=long_day1-long_day2+1;
 T->days=HEX_to_BCD(day);
 
 long_day1=timer_int%86400;
 i=long_day1/3600;
 T->hours=HEX_to_BCD(i);

 i=long_day1%3600/60;
 T->minutes=HEX_to_BCD(i);
 
 i=long_day1%60;
 T->seconds=HEX_to_BCD(i);
}


/*********************************************************************************
*��������:uint16_t data_to_buf( uint8_t * pdest, uint32_t data, uint8_t width )
*��������:����ͬ���͵����ݴ���buf�У�������buf��Ϊ���ģʽ
*��	��:	pdest: 	������ݵ�buffer
		data:	������ݵ�ԭʼ����
		width:	��ŵ�ԭʼ����ռ�õ�buf�ֽ���
*��	��:	 
*�� �� ֵ:������ֽ���
*��	��:������
*��������:2013-06-5
*---------------------------------------------------------------------------------
*�� �� ��:
*�޸�����:
*�޸�����:
*********************************************************************************/
uint16_t data_to_buf( uint8_t * pdest, uint32_t data, uint8_t width )
{
	u8 *buf;
	u8 i;
	buf=pdest;

	switch( width )
	{
		case 1:
			*buf++ = data & 0xff;
			break;
		case 2:
			*buf++ = data >> 8;
			*buf++ = data & 0xff;
			break;
		case 4:
			*buf++ = data >> 24;
			*buf++ = data >> 16;
			*buf++ = data >> 8;
			*buf++ = data & 0xff;
			break;
	}
	return width;
}



/*********************************************************************************
*��������:uint16_t buf_to_data( uint8_t * psrc, uint8_t width )
*��������:����ͬ���͵����ݴ�buf��ȡ������������buf��Ϊ���ģʽ
*��	��:	psrc: 	������ݵ�buffer
		width:	��ŵ�ԭʼ����ռ�õ�buf�ֽ���
*��	��:	 none
*�� �� ֵ:uint32_t ���ش洢������
*��	��:������
*��������:2013-06-5
*---------------------------------------------------------------------------------
*�� �� ��:
*�޸�����:
*�޸�����:
*********************************************************************************/
uint32_t buf_to_data( uint8_t * psrc, uint8_t width )
{
	u8 i;
	u8 *buf;
	u32 outData=0;
	buf=psrc;
	for(i=0;i<width;i++)
		{
		outData<<=8;
		outData+=*psrc++;
		}
	return outData;
}

/*********************************************************************************
*��������:u32 Cam_Flash_AddrCheck(u32 pro_Address)
*��������:��鵱ǰ��λ���Ƿ�Ϸ������Ϸ����޸�Ϊ�Ϸ����ݲ�����
*��	��:none
*��	��:none 
*�� �� ֵ:��ȷ�ĵ�ֵַ
*��	��:������
*��������:2013-06-5
*---------------------------------------------------------------------------------
*�� �� ��:
*�޸�����:
*�޸�����:
*********************************************************************************/
static u32 Cam_Flash_AddrCheck(u32 pro_Address)
{
	while(pro_Address>=DF_CamAddress_End)
		{
		pro_Address=pro_Address+DF_CamAddress_Start-DF_CamAddress_End;
		}
	if(pro_Address<DF_CamAddress_Start)
		{
		pro_Address=DF_CamAddress_Start;
		}
	return pro_Address;
}



/*********************************************************************************
*��������:u8 Cam_Flash_FirstPicProc(u32 temp_wr_addr)
*��������:�޸ĵ�1��ͼƬ����λ�ò�������ΪҪ��flash����erase����erase���ǵ�1��ͼƬʱ����Ҫ��DF_PicParameter��
		��һ��ͼƬ����Ϣ����
*��	��:��ǰҪ��д��λ��
*��	��:none 
*�� �� ֵ:1��ʾ�������أ�0��ʾ�д�����
*��	��:������
*��������:2013-06-5
*---------------------------------------------------------------------------------
*�� �� ��:
*�޸�����:
*�޸�����:
*********************************************************************************/
static u8 Cam_Flash_FirstPicProc(u32 temp_wr_addr)
{
	u8 i;
	u8 ret;
	u32 TempAddress,u32TempData;
	TypeDF_PackageHead TempPackageHead;

	if((Cam_Flash_AddrCheck(temp_wr_addr)==(DF_PicParameter.FirstPic.Address&0xFFFFF000))&&(DF_PicParameter.Number))
		{
		TempAddress=(DF_PicParameter.FirstPic.Address+DF_PicParameter.FirstPic.Len+DF_CamSaveSect-1)/DF_CamSaveSect*DF_CamSaveSect;
		for(i=0;i<8;i++)
			{
			sst25_read(Cam_Flash_AddrCheck(TempAddress),(u8 *)&TempPackageHead,sizeof(TempPackageHead));
			if(strncmp(TempPackageHead.Head,CAM_HEAD,strlen(CAM_HEAD))==0)
				{
				if((TempAddress & 0xFFFFF000)!= (DF_PicParameter.FirstPic.Address & 0xFFFFF000))
					{
					DF_PicParameter.FirstPic.Address=Cam_Flash_AddrCheck(TempAddress);
					DF_PicParameter.FirstPic.Len=TempPackageHead.Len;
					DF_PicParameter.FirstPic.Data_ID=TempPackageHead.Data_ID;
					DF_PicParameter.Number--;
					ret = 1;
					break;
					}
				if(TempPackageHead.Len==0)
					{
					ret =  0;
					break;
					}
				TempAddress+=(TempPackageHead.Len+DF_CamSaveSect-1)/DF_CamSaveSect*DF_CamSaveSect;
				}
			else
				{
				ret =  0;
				break;
				}
			}
		ret =  0;
		}
	else
		{
		ret =  1;
		}
	sst25_erase_4k(temp_wr_addr);
	return ret;
}


/*********************************************************************************
*��������:u16 Cam_Flash_InitPara(void)
*��������:��ʼ��Pic������������ȡFLASH�е�ͼƬ��Ϣ����ȡ��ͼƬ����������ʼλ�ã�����λ�õȣ�
		��Щ��ȡ�������ݶ��洢�� DF_PicParameter �С�
*��	��:none
*��	��:none 
*�� �� ֵ:��Ч��ͼƬ����
*��	��:������
*��������:2013-06-5
*---------------------------------------------------------------------------------
*�� �� ��:
*�޸�����:
*�޸�����:
*********************************************************************************/
static u16 Cam_Flash_InitPara(void)
{
 u32 TempAddress,u32TempData;
 TypeDF_PackageHead TempPackageHead;
 TypeDF_PackageInfo TempPackageInfo;
 
 
 memset(&DF_PicParameter,0,sizeof(DF_PicParameter));
 DF_PicParameter.FirstPic.Address=DF_CamAddress_Start;
 DF_PicParameter.LastPic.Address=DF_CamAddress_Start;
 for(TempAddress=DF_CamAddress_Start;TempAddress<DF_CamAddress_End;)
 	{
 	sst25_read(TempAddress,(u8 *)&TempPackageHead,sizeof(TempPackageHead));
	if(strncmp(TempPackageHead.Head,CAM_HEAD,strlen(CAM_HEAD))==0)
		{
		DF_PicParameter.Number++;
		TempPackageInfo.Address=TempAddress;
		TempPackageInfo.Data_ID=TempPackageHead.Data_ID;
		TempPackageInfo.Len=TempPackageHead.Len;
		if(DF_PicParameter.Number==1)
			{
			DF_PicParameter.FirstPic=TempPackageInfo;
			DF_PicParameter.LastPic =TempPackageInfo;
			}
		else
			{
			if(TempPackageInfo.Data_ID>DF_PicParameter.LastPic.Data_ID)
				{
				DF_PicParameter.LastPic =TempPackageInfo;
				}
			else if(TempPackageInfo.Data_ID<DF_PicParameter.FirstPic.Data_ID)
				{
				DF_PicParameter.FirstPic=TempPackageInfo;
				}
			}
		TempAddress+=(TempPackageInfo.Len+DF_CamSaveSect-1)/DF_CamSaveSect*DF_CamSaveSect;
		}
	else
		{
		TempAddress+=DF_CamSaveSect;
		}
 	}
 return DF_PicParameter.Number;
}



/*********************************************************************************
*��������:rt_err_t Cam_Flash_WrPic(u8 *pData,u16 len, TypeDF_PackageHead *pHead)
*��������:��FLASH��д��ͼƬ���ݣ�������ݵ����ݵ�pHead->lenΪ��0ֵ��ʾΪ���һ������
*��	��:	pData:д�������ָ�룬ָ������buf��
		len:���ݳ��ȣ�ע�⣬�ó��ȱ���С��4096
		pHead:���ݰ�ͷ��Ϣ���ð�ͷ��Ϣ��Ҫ����ʱ��Timer����ֵÿ�ζ����봫�ݣ�����ͬ���İ���ֵ���ܱ仯��
				���һ�������轫len����Ϊ���ݳ���len�ĳ��Ȱ�����ͷ���֣���ͷ����Ϊ�̶�64�ֽڣ�������
				lenΪ0.
*��	��:none 
*�� �� ֵ:re_err_t
*��	��:������
*��������:2013-06-5
*---------------------------------------------------------------------------------
*�� �� ��:
*�޸�����:
*�޸�����:
*********************************************************************************/
static rt_err_t Cam_Flash_WrPic(u8 *pData,u16 len, TypeDF_PackageHead *pHead)
{
	u16 i;
	u32 temp_wr_addr;
	u32 temp_Len;
	static u8	WriteFuncUserBack=0;
	static u32	WriteAddress=0;
	static u32	WriteAddressStart=0;
	static T_TIMES	LastTime={0,0,0,0,0,0};
	
	u8 strBuf[256];
	
	
	if(memcmp(&LastTime,&pHead->Time,sizeof(LastTime))!=0)
		{
		LastTime=pHead->Time;
		WriteAddressStart=(DF_PicParameter.LastPic.Address+DF_PicParameter.LastPic.Len+DF_CamSaveSect-1)/DF_CamSaveSect*DF_CamSaveSect;
		WriteAddressStart=Cam_Flash_AddrCheck(WriteAddressStart);
		WriteAddress=WriteAddressStart+64;
		if((WriteAddressStart&0xFFF)==0)
			{
			Cam_Flash_FirstPicProc(WriteAddressStart);
			}
		else
			{
			///�жϸ������Ƿ������쳣���쳣��Ҫʹ��falshд����sst25_write_back
			sst25_read(WriteAddressStart,strBuf,sizeof(strBuf));
			WriteFuncUserBack=0;
			for(i=0;i<sizeof(strBuf);i++)
				{
				if(strBuf[i]!=0xFF)
					{
					if(i<64)		///�����ͷ����Ҳ������հ�ͷ��������Ϊ0xFF
						{
						memset(strBuf,0xFF,sizeof(strBuf));
						sst25_write_back(WriteAddressStart,strBuf,sizeof(strBuf));
						}
					
					WriteFuncUserBack=1;
					break;
					}
				}
			}
		}
	
	if(WriteFuncUserBack)
		{
		if((WriteAddress&0xFFFFF000)!=((WriteAddress+len)&0xFFFFF000))		///��Խ���������Ĵ���
			{
			temp_wr_addr=(WriteAddress+len)&0xFFFFF000;
			temp_Len=temp_wr_addr-WriteAddress;
			sst25_write_back(Cam_Flash_AddrCheck(WriteAddress),pData,temp_Len);
			Cam_Flash_FirstPicProc(Cam_Flash_AddrCheck(temp_wr_addr));
			sst25_write_through(Cam_Flash_AddrCheck(temp_wr_addr),pData+temp_Len,len-temp_Len);
			WriteFuncUserBack=0;
			}
		else
			{
			sst25_write_back(Cam_Flash_AddrCheck(WriteAddress),pData,len);
			}
		}
	else
		{
		if((WriteAddress&0xFFFFF000)!=((WriteAddress+len)&0xFFFFF000))		///��Խ���������Ĵ���
			{
			temp_wr_addr=(WriteAddress+len)&0xFFFFF000;
			temp_Len=temp_wr_addr-WriteAddress;
			sst25_write_through(Cam_Flash_AddrCheck(WriteAddress),pData,temp_Len);
			Cam_Flash_FirstPicProc(Cam_Flash_AddrCheck(temp_wr_addr));
			sst25_write_through(Cam_Flash_AddrCheck(temp_wr_addr),pData+temp_Len,len-temp_Len);
			}
		else
			{
			sst25_write_through(Cam_Flash_AddrCheck(WriteAddress),pData,len);
			}
		}
	WriteAddress+=len;
 	if(pHead->Len)
 		{
 		WriteAddress=WriteAddressStart;
 		DF_PicParameter.Number++;
 		DF_PicParameter.LastPic.Data_ID++;
		DF_PicParameter.LastPic.Address=Cam_Flash_AddrCheck(WriteAddressStart);
		DF_PicParameter.LastPic.Len=pHead->Len;
		if(DF_PicParameter.Number==1)
			{
			DF_PicParameter.FirstPic=DF_PicParameter.LastPic;
			}
		
		memcpy(pHead->Head,CAM_HEAD,strlen(CAM_HEAD));
		pHead->Data_ID=DF_PicParameter.LastPic.Data_ID;
		pHead->Media_Style=0;
		pHead->Media_Format=0;
		sst25_write_through(Cam_Flash_AddrCheck(WriteAddressStart),(u8 *)pHead,sizeof(TypeDF_PackageHead));
 		}
	return RT_EOK;
}



/*********************************************************************************
*��������:u32 Cam_Flash_FindPicID(u32 id,TypeDF_PackageHead *p_head)
*��������:��FLASH�в���ָ��ID��ͼƬ������ͼƬ��ͷ���´洢��p_head��
*��	��:	id:��ý��ID��
		p_head:������������ΪͼƬ��ý�����ݰ�ͷ��Ϣ(����ΪTypeDF_PackageHead)
*��	��:p_head 
*�� �� ֵ:u32 0xFFFFFFFF��ʾû���ҵ�ͼƬ��������ʾ�ҵ���ͼƬ������ֵΪͼƬ��flash�еĵ�ַ
*��	��:������
*��������:2013-06-5
*---------------------------------------------------------------------------------
*�� �� ��:
*�޸�����:
*�޸�����:
*********************************************************************************/
u32 Cam_Flash_FindPicID(u32 id,TypeDF_PackageHead *p_head)
{
	u32 i;
	u32 TempAddress;
	
	static TypeDF_PackageInfo lastPackInfo={0xFFFFFFFF,0,0xFFFFFFFF};
	static TypeDF_PackageHead TempPackageHead;
	
	if(DF_PicParameter.Number==0)
		return 0xFFFFFFFF;
	if((id<DF_PicParameter.FirstPic.Data_ID)||(id>DF_PicParameter.LastPic.Data_ID))
		return 0xFFFFFFFF;
	if(id!=lastPackInfo.Data_ID)
		{
		lastPackInfo.Data_ID=0xFFFFFFFF;
		TempAddress=DF_PicParameter.FirstPic.Address;
		for(i=0;i<DF_PicParameter.Number;i++)
			{
			TempAddress=Cam_Flash_AddrCheck(TempAddress);
			sst25_read(TempAddress,(u8 *)&TempPackageHead,sizeof(TypeDF_PackageHead));
			if(strncmp(TempPackageHead.Head,CAM_HEAD,strlen(CAM_HEAD))==0)
				{
				///�鿴��ͼƬ�Ƿ�ɾ��
				if(TempPackageHead.State & BIT(0))
					{
					if(TempPackageHead.Data_ID==id)
						{
						lastPackInfo.Data_ID=id;
						lastPackInfo.Address=TempAddress;
						lastPackInfo.Len=TempPackageHead.Len;
						memcpy((void *)p_head,(void *)&TempPackageHead,sizeof(TypeDF_PackageHead));
						return lastPackInfo.Address;
						}
					}
				TempAddress+=(TempPackageHead.Len+DF_CamSaveSect-1)/DF_CamSaveSect*DF_CamSaveSect;
				}
			else
				{
				return 0xFFFFFFFF;
				}
			}
		}
	else
		{
		memcpy((void *)p_head,(void *)&TempPackageHead,sizeof(TypeDF_PackageHead));
		return lastPackInfo.Address;
		}
	return 0xFFFFFFFF;
}




/*********************************************************************************
*��������:rt_err_t Cam_Flash_RdPic(void *pData,u16 *len, u32 id,u8 offset )
*��������:��FLASH�ж�ȡͼƬ����
*��	��:	pData:д�������ָ�룬ָ������buf��
		len:���ص����ݳ���ָ��ע�⣬�ó������Ϊ512
		id:��ý��ID��
		offset:��ý������ƫ��������0��ʼ��0��ʾ��ȡ��ý��ͼƬ��ͷ��Ϣ����ͷ��Ϣ���ȹ̶�Ϊ64�ֽڣ�����
				�ṹ��TypeDF_PackageHead��ʽ�洢
*��	��:none 
*�� �� ֵ:re_err_t
*��	��:������
*��������:2013-06-5
*---------------------------------------------------------------------------------
*�� �� ��:
*�޸�����:
*�޸�����:
*********************************************************************************/
rt_err_t Cam_Flash_RdPic_20130605(void *pData,u16 *len, u32 id,u8 offset )
{
	u16 i;
	u32 TempAddress;
	u32 temp_Len;
	TypeDF_PackageHead TempPackageHead;
	static TypeDF_PackageInfo lastPackInfo={0,0,0};

	*len=0;
	
	if(DF_PicParameter.Number==0)
		return RT_ERROR;
	if((id<DF_PicParameter.FirstPic.Data_ID)||(id>DF_PicParameter.LastPic.Data_ID))
		return RT_ERROR;
	if(id!=lastPackInfo.Data_ID)
		{
		lastPackInfo.Data_ID=0xFFFFFFFF;
		TempAddress=DF_PicParameter.FirstPic.Address;
		for(i=0;i<DF_PicParameter.Number;i++)
			{
			TempAddress=Cam_Flash_AddrCheck(TempAddress);
			sst25_read(TempAddress,(u8 *)&TempPackageHead,sizeof(TempPackageHead));
			if(strncmp(TempPackageHead.Head,CAM_HEAD,strlen(CAM_HEAD))==0)
				{
				if(TempPackageHead.Data_ID==id)
					{
					lastPackInfo.Data_ID=id;
					lastPackInfo.Address=TempAddress;
					lastPackInfo.Len=TempPackageHead.Len;
					break;
					}
				TempAddress+=(TempPackageHead.Len+DF_CamSaveSect-1)/DF_CamSaveSect*DF_CamSaveSect;
				}
			else
				{
				return RT_ERROR;
				}
			}
		}

	if(lastPackInfo.Data_ID == 0xFFFFFFFF)
		{
		return RT_ERROR;
		}
	if(offset>(lastPackInfo.Len-1)/512)
		{
		return RT_ENOMEM;
		}
	if(offset==(lastPackInfo.Len-1)/512)
		{
		temp_Len=lastPackInfo.Len-(offset*512);
		}
	else
		{
		temp_Len=512;
		}
	sst25_read(lastPackInfo.Address+offset*512,(u8 *)pData,temp_Len);
	*len=temp_Len;
	return RT_EOK;
}


/*********************************************************************************
*��������:rt_err_t Cam_Flash_RdPic(void *pData,u16 *len, u32 id,u8 offset )
*��������:��FLASH�ж�ȡͼƬ����
*��	��:	pData:д�������ָ�룬ָ������buf��
		len:���ص����ݳ���ָ��ע�⣬�ó������Ϊ512
		id:��ý��ID��
		offset:��ý������ƫ��������0��ʼ��0��ʾ��ȡ��ý��ͼƬ��ͷ��Ϣ����ͷ��Ϣ���ȹ̶�Ϊ64�ֽڣ�����
				�ṹ��TypeDF_PackageHead��ʽ�洢
*��	��:none 
*�� �� ֵ:re_err_t
*��	��:������
*��������:2013-06-5
*---------------------------------------------------------------------------------
*�� �� ��:
*�޸�����:
*�޸�����:
*********************************************************************************/
rt_err_t Cam_Flash_RdPic(void *pData,u16 *len, u32 id,u8 offset )
{
	u16 i;
	u32 TempAddress;
	u32 temp_Len;
	TypeDF_PackageHead TempPackageHead;
	
	*len=0;
	
	TempAddress=Cam_Flash_FindPicID(id,&TempPackageHead);
	if(TempAddress==0xFFFFFFFF)
		{
		return RT_ERROR;
		}
	if(TempPackageHead.Data_ID == 0xFFFFFFFF)
		{
		return RT_ERROR;
		}
	if(offset>(TempPackageHead.Len-1)/512)
		{
		return RT_ENOMEM;
		}
	if(offset==(TempPackageHead.Len-1)/512)
		{
		temp_Len=TempPackageHead.Len-(offset*512);
		}
	else
		{
		temp_Len=512;
		}
	sst25_read(TempAddress+offset*512,(u8 *)pData,temp_Len);
	*len=temp_Len;
	return RT_EOK;
}



/*********************************************************************************
*��������:rt_err_t Cam_Flash_DelPic(u32 id)
*��������:��FLASH��ɾ��ͼƬ��ʵ���ϲ�û��ɾ�������ǽ�ɾ�������0�������´ξ���Ҳ����ѯ��ͼƬ
*��	��:	id:��ý��ID��
*��	��:none 
*�� �� ֵ:re_err_t
*��	��:������
*��������:2013-06-13
*---------------------------------------------------------------------------------
*�� �� ��:
*�޸�����:
*�޸�����:
*********************************************************************************/
rt_err_t Cam_Flash_DelPic(u32 id )
{
	u32 TempAddress;
	TypeDF_PackageHead TempPackageHead;
	
	TempAddress=Cam_Flash_FindPicID(id,&TempPackageHead);
	if(TempAddress==0xFFFFFFFF)
		{
		return RT_ERROR;
		}
	TempPackageHead.State &= ~(BIT(0));
	sst25_write_through(TempAddress,(u8 *)&TempPackageHead,sizeof(TypeDF_PackageHead));
	return RT_EOK;
}

/*********************************************************************************
*��������:rt_err_t Cam_Flash_TransOkSet(u32 id)
*��������:����ID��ͼƬ���Ϊ���ͳɹ�
*��	��:	id:��ý��ID��
*��	��:none 
*�� �� ֵ:re_err_t
*��	��:������
*��������:2013-06-13
*---------------------------------------------------------------------------------
*�� �� ��:
*�޸�����:
*�޸�����:
*********************************************************************************/
rt_err_t Cam_Flash_TransOkSet(u32 id )
{
	u32 TempAddress;
	TypeDF_PackageHead TempPackageHead;
	
	TempAddress=Cam_Flash_FindPicID(id,&TempPackageHead);
	if(TempAddress==0xFFFFFFFF)
		{
		return RT_ERROR;
		}
	TempPackageHead.State &= ~(BIT(1));
	sst25_write_through(TempAddress,(u8 *)&TempPackageHead,sizeof(TypeDF_PackageHead));
	return RT_EOK;
}


/*********************************************************************************
*��������:u16 Cam_Flash_SearchPic(T_TIMES *start_time,T_TIMES *end_time,Style_Cam_Requset_Para *para,u8 *psrc)
*��������:��FLASH�в���ָ��ʱ��ε�ͼƬ����
*��	��:	start_time	��ʼʱ�䣬
		end_time		����ʱ�䣬
		para			����ͼƬ������
		para			�洢���ҵ���ͼƬ��λ�ã�
*��	��: u16 ���ͣ���ʾ���ҵ���ͼƬ���� 
*�� �� ֵ:re_err_t
*��	��:������
*��������:2013-06-5
*---------------------------------------------------------------------------------
*�� �� ��:
*�޸�����:
*�޸�����:
*********************************************************************************/
u16 Cam_Flash_SearchPic(T_TIMES *start_time,T_TIMES *end_time,TypeDF_PackageHead *para,u8 *pdest)
{
	u16 i;
	u32 TempAddress;
	u32 temp_u32;
	u32 temp_Len;
	TypeDF_PackageHead TempPackageHead;
	u16 ret_num=0;
	
	if(DF_PicParameter.Number==0)
		return 0;

	TempAddress=DF_PicParameter.FirstPic.Address;
	for(i=0;i<DF_PicParameter.Number;i++)
		{
		TempAddress=Cam_Flash_AddrCheck(TempAddress);
		sst25_read(TempAddress,(u8 *)&TempPackageHead,sizeof(TempPackageHead));
		if(strncmp(TempPackageHead.Head,CAM_HEAD,strlen(CAM_HEAD))==0)
			{
			///�鿴��ͼƬ�Ƿ�ɾ��
			if(TempPackageHead.State & BIT(0))
				{
				///�Ƚ϶�ý�����ͣ���ý��ͨ������ý�崥��Դ
				if((TempPackageHead.Media_Style==para->Media_Style)&&((TempPackageHead.Channel_ID==para->Channel_ID)||(para->Channel_ID==0))&&((TempPackageHead.TiggerStyle==para->TiggerStyle)||(para->TiggerStyle==0xFF)))
					{
					temp_u32=Times_To_LongInt(&TempPackageHead.Time);
					///�Ƚ�ʱ���Ƿ��ڷ�Χ
					if((temp_u32>Times_To_LongInt(start_time))&&(temp_u32<=Times_To_LongInt(end_time)))
						{
						///�ҵ�������
						data_to_buf(pdest,TempAddress,4);
						//memcpy(pdest,&TempAddress,4);
						pdest+=4;
						ret_num++;
						}
					}
				}
			TempAddress+=(TempPackageHead.Len+DF_CamSaveSect-1)/DF_CamSaveSect*DF_CamSaveSect;
			}
		else
			{
			return ret_num;
			}
		}
	
	return ret_num;
}

/*********************************************************************************
*��������:static rt_err_t Cam_Device_open( void )
*��������:��ģ�鹩��
*��	��:none
*��	��:none 
*�� �� ֵ:none
*��	��:������
*��������:2013-06-3
*---------------------------------------------------------------------------------
*�� �� ��:
*�޸�����:
*�޸�����:
*********************************************************************************/
static rt_err_t Cam_Device_open( void )
{
	Power_485CH1_ON;
	return RT_EOK;
}


/*********************************************************************************
*��������:static rt_err_t Cam_Devic_close( void )
*��������:�ر�ģ�鹩��
*��	��:none
*��	��:none 
*�� �� ֵ:none
*��	��:������
*��������:2013-06-3
*---------------------------------------------------------------------------------
*�� �� ��:
*�޸�����:
*�޸�����:
*********************************************************************************/
static rt_err_t Cam_Devic_close( void )
{
	Power_485CH1_OFF;
	return RT_EOK;
}




/*********************************************************************************
*��������:void Cam_Device_init( void )
*��������:��ʼ��CAMģ����ؽӿ�
*��	��:none
*��	��:none 
*�� �� ֵ:none
*��	��:������
*��������:2013-06-3
*---------------------------------------------------------------------------------
*�� �� ��:
*�޸�����:
*�޸�����:
*********************************************************************************/
void Cam_Device_init( void )
{
	GPIO_InitTypeDef	GPIO_InitStructure;
	///����
	Power_485CH1_ON;

	///��ʼ����Ϣ����
	rt_mq_init( &mq_Cam, "mq_cam", &msgpool_cam[0], sizeof(Style_Cam_Requset_Para), sizeof(msgpool_cam), RT_IPC_FLAG_FIFO );

	///��ʼ��flash����
	Cam_Flash_InitPara();

	///��ʼ������״̬����
	Current_Cam_Para.Retry=0;
	Current_Cam_Para.State=CAM_IDLE;
}

/*********************************************************************************
*��������:static void Cam_Start_Cmd(u16 Cam_ID)
*��������:��cameraģ�鷢�Ϳ�ʼ����ָ��
*��	��:Cam_ID	��Ҫ���յ�camera�豸ID
*��	��:none 
*�� �� ֵ:none
*��	��:������
*��������:2013-06-3
*---------------------------------------------------------------------------------
*�� �� ��:
*�޸�����:
*�޸�����:
*********************************************************************************/
static void Cam_Start_Cmd(u16 Cam_ID)
{
 	u8 Take_photo[10]= { 0x40, 0x40, 0x61, 0x81, 0x02, 0X00, 0X00, 0X02, 0X0D, 0X0A };	 //----  ������������
 	Take_photo[4]=(u8)Cam_ID;
 	Take_photo[5]=(u8)(Cam_ID>>8);
	RS485_write(Take_photo,10);
	uart2_rxbuf_rd=uart2_rxbuf_wr;
	rt_kprintf("\r\n������������");
}

/*********************************************************************************
*��������:static void Cam_Read_Cmd(u16 Cam_ID)
*��������:��cameraģ�鷢�Ͷ�ȡ��Ƭ����ָ��
*��	��:Cam_ID	��Ҫ���յ�camera�豸ID
*��	��:none 
*�� �� ֵ:none
*��	��:������
*��������:2013-06-3
*---------------------------------------------------------------------------------
*�� �� ��:
*�޸�����:
*�޸�����:
*********************************************************************************/
static void Cam_Read_Cmd(u16 Cam_ID)
{
	uint8_t Fectch_photo[10] = { 0x40, 0x40, 0x62, 0x81, 0x02, 0X00, 0XFF, 0XFF, 0X0D, 0X0A };  //----- ����ȡͼ����
	Fectch_photo[4]=(u8)Cam_ID;
 	Fectch_photo[5]=(u8)(Cam_ID>>8);
	RS485_write(Fectch_photo,10);
	uart2_rxbuf_rd=uart2_rxbuf_wr;
	rt_kprintf("\r\n��ȡ��Ƭ����");
}


/*********************************************************************************
*��������:rt_err_t take_pic_request( Style_Cam_Requset_Para *para)
*��������:��������ָ��
*��	��:para���ղ���
*��	��:none 
*�� �� ֵ:none
*��	��:������
*��������:2013-06-3
*---------------------------------------------------------------------------------
*�� �� ��:
*�޸�����:
*�޸�����:
*********************************************************************************/
rt_err_t take_pic_request( Style_Cam_Requset_Para *para)
{
 return rt_mq_send(&mq_Cam,(void *) para,sizeof(Style_Cam_Requset_Para));
}


void takepic(u16 id)
{
 Style_Cam_Requset_Para tempPara;
 memset(&tempPara,0,sizeof(tempPara));
 tempPara.Channel_ID=id;
 tempPara.PhoteSpace=0;
 tempPara.PhotoTotal=1;
 tempPara.SavePhoto=1;
 tempPara.TiggerStyle=Cam_TRIGGER_PLANTFORM;
 take_pic_request(&tempPara);
 rt_kprintf("\r\n��������ID=%d",id);
}
FINSH_FUNCTION_EXPORT( takepic, takepic);

void readpic(u16 id)
{
 u16 i;
 u16 len;
 u8 *pbuf;
 Style_Cam_Requset_Para tempPara;
 memset(&tempPara,0,sizeof(tempPara));
 tempPara.Channel_ID=id;
 tempPara.PhoteSpace=0;
 tempPara.PhotoTotal=1;
 tempPara.SavePhoto=1;
 tempPara.TiggerStyle=0;
 pbuf=rt_malloc(1024);
 rt_kprintf("\r\n��������:\r\n");
 for(i=0;i<100;i++)
 	{
 	if(RT_EOK==Cam_Flash_RdPic(pbuf,&len,id,i))
 		{
		Hex_To_Ascii(pbuf,CamTestBuf,len);
		rt_kprintf(CamTestBuf);
 		}
	else
		{
		break;
		}
 	}
 rt_free(pbuf);
}
FINSH_FUNCTION_EXPORT( readpic, readpic);


void getpicpara(void)
{
 
 rt_kprintf("\r\n pic Number=%d \r\n Data_ID_1=%d \r\n Address_1=%d \r\n Len_1   =%d \r\n Data_ID_2=%d \r\n Address_2=%d \r\n Len_2   =%d",
 			DF_PicParameter.Number,
 			DF_PicParameter.FirstPic.Data_ID,
 			DF_PicParameter.FirstPic.Address,
 			DF_PicParameter.FirstPic.Len,
 			DF_PicParameter.LastPic.Data_ID,
 			DF_PicParameter.LastPic.Address,
 			DF_PicParameter.LastPic.Len);
}
FINSH_FUNCTION_EXPORT( getpicpara, getpicpara);




static void delay_us( const uint32_t usec )
{
	__IO uint32_t	count	= 0;
	const uint32_t	utime	= ( 168 * usec / 7 );
	do
	{
		if( ++count > utime )
		{
			return;
		}
	}
	while( 1 );
}


static bool Cam_Check_Para(Style_Cam_Requset_Para *para)
{
 if((para->Channel_ID<=15)||(para->Channel_ID==0xFFFF))
 	{
 	if(para->PhotoNum < para->PhotoTotal)
		return  true;
 	}
 return false;
}


/*********************************************************************************
*��������:bool Camera_RX_Data(u16 *RxLen)
*��������:���ս������ݴ���
*��	��:RxLen:��ʾ���յ������ݳ���ָ��
*��	��:RxLen:��ʾ���յ������ݳ���ָ�� 
*�� �� ֵ:0��ʾ���ս�����,1��ʾ���ճɹ�
*��	��:������
*��������:2013-06-3
*---------------------------------------------------------------------------------
*�� �� ��:
*�޸�����:
*�޸�����:
*********************************************************************************/
static u8 Camera_RX_Data(u16 *RxLen)
{
	u8 ch;
	static u16 page_size=0;
	static u16 cam_rx_head_wr=0;
	
	/*�����Ƿ��յ�����*/
	while( RS485_read_char(&ch) )
	{
		switch(Current_Cam_Para.Rx_State)
		{
			case RX_DATA: /*������Ϣ��ʽ: λ��(2B) ��С(2B) FLAG_FF ���� 0D 0A*/
				uart2_rx[uart2_rx_wr++] = ch;
				uart2_rx_wr 			%= UART2_RX_SIZE;
				if(uart2_rx_wr==page_size) Current_Cam_Para.Rx_State=RX_FCS;
				break;	
			case RX_IDLE:
				if(ch==0x40) Current_Cam_Para.Rx_State=RX_SYNC1;
				break;
			case RX_SYNC1:
				if(ch==0x40) Current_Cam_Para.Rx_State=RX_SYNC2;
				else Current_Cam_Para.Rx_State=RX_IDLE;
				break;
			case RX_SYNC2:
				if(ch==0x63)
				{
					cam_rx_head_wr=0;
					Current_Cam_Para.Rx_State=RX_HEAD;
				}	
				else 
					Current_Cam_Para.Rx_State=RX_IDLE;
				break;
			case RX_HEAD:
				uart2_rx[cam_rx_head_wr++]=ch;
				if(cam_rx_head_wr==5)
				{
					uart2_rx_wr=0;
					page_size=(uart2_rx[3]<<8)|uart2_rx[2];
					Current_Cam_Para.Rx_State=RX_DATA;
				}
				break;
			case RX_FCS:
				Current_Cam_Para.Rx_State=RX_0D;
				break;		
			case RX_0D:
				if(ch==0x0d) Current_Cam_Para.Rx_State=RX_0A;
				else Current_Cam_Para.Rx_State=RX_IDLE;
				break;	
			case RX_0A:
				Current_Cam_Para.Rx_State=RX_IDLE;
				if(ch==0x0a) return 1;
				break;	
		}
	}
	return 0;
}




/*********************************************************************************
*��������:void Camera_Process(void)
*��������:����������ش���(������:���գ��洢��Ƭ���������ս���ָ���808)
*��	��:none
*��	��:none 
*�� �� ֵ:none
*��	��:������
*��������:2013-06-3
*---------------------------------------------------------------------------------
*�� �� ��:
*�޸�����:
*�޸�����:
*********************************************************************************/
u8 Camera_Process(void)
{
 u16 RxLen;
 static u16 cam_photo_size;
 static u8 cam_page_num=0;
 u8 cam_last_page=0;
 static uint32_t tick;
 static TypeDF_PackageHead pack_head;
 
 switch(Current_Cam_Para.State)
	{
	case CAM_IDLE:
		{
			if(RT_EOK == rt_mq_recv(&mq_Cam,(void *)&Current_Cam_Para.Para,sizeof(Style_Cam_Requset_Para),RT_WAITING_NO))
				{
				Current_Cam_Para.Retry=0;
				Current_Cam_Para.State=CAM_START;
				rt_kprintf("\r\n�յ�������Ϣ");
				}
			else
				{
				return 0;
				}
		}
	case CAM_START:
		{
			if(Current_Cam_Para.Retry>=3)
				{
				Current_Cam_Para.State=CAM_FALSE;
				break;
				}
			Current_Cam_Para.Retry++;
			memset(&pack_head,0,sizeof(pack_head));
			cam_page_num=0;
			cam_photo_size=0;
			tick=rt_tick_get();
			Cam_Start_Cmd(Current_Cam_Para.Para.Channel_ID);
			Current_Cam_Para.State=CAM_RX_PHOTO;
			Current_Cam_Para.Rx_State=RX_IDLE;
			break;
		}
	case CAM_GET_PHOTO:
		{
			tick=rt_tick_get();
			Cam_Read_Cmd(Current_Cam_Para.Para.Channel_ID);
			Current_Cam_Para.State=CAM_RX_PHOTO;
			Current_Cam_Para.Rx_State=RX_IDLE;
			break;
		}
	case CAM_RX_PHOTO:
		{
			if(1==Camera_RX_Data(&RxLen))
			{
				rt_kprintf("\r\n���յ���������!");
				tick=rt_tick_get();
				///�յ�����,�洢,�ж��Ƿ�ͼƬ����
				if(uart2_rx_wr>512)		///���ݴ���512,�Ƿ�
				{
					rt_kprintf("\r\nCAM%d invalided\r\n",Current_Cam_Para.Para.Channel_ID);
					Current_Cam_Para.State=CAM_START;
					break;
				}
				if(uart2_rx_wr==512)
				{
					if((uart2_rx[510]==0xff)&&(uart2_rx[511]==0xD9))
					{
						cam_last_page=1;
					}
				}
				else
				{
					cam_last_page=1;
				}
				
				cam_page_num++;
				cam_photo_size+=uart2_rx_wr;
				///��һ�����ݣ���Ҫ��д������Ȼ��洢����ȷ
				if(cam_page_num==1)
				{
					pack_head.Channel_ID=Current_Cam_Para.Para.Channel_ID;
					pack_head.TiggerStyle=Current_Cam_Para.Para.TiggerStyle;
					pack_head.Media_Format=0;
					pack_head.Media_Style=0;
					
					pack_head.Time.years=0x13;
					pack_head.Time.months=0x06;
					pack_head.Time.days=tick>>24;
					pack_head.Time.hours=tick>>16;
					pack_head.Time.minutes=tick>>8;
					pack_head.Time.seconds=tick;
					pack_head.State=0xFF;
					if(Current_Cam_Para.Para.SavePhoto)
						{
						pack_head.State &= ~(BIT(2));
						}
				}
				///���һ�����ݣ���Ҫ������д�롣
				if(cam_last_page)
				{
					pack_head.Len=cam_photo_size+64;
					Current_Cam_Para.State=CAM_END;
				}
				else
				{
					Current_Cam_Para.State=CAM_GET_PHOTO;
				}
				///��������
				Cam_Flash_WrPic(uart2_rx,uart2_rx_wr,&pack_head);
				Hex_To_Ascii(uart2_rx,CamTestBuf,uart2_rx_wr);
				rt_kprintf("\r\n��������:\r\n");
				rt_kprintf("%s\r\n",CamTestBuf);
				rt_kprintf("\r\n%d>CAM%d photo %d bytes",rt_tick_get()*10,Current_Cam_Para.Para.Channel_ID,cam_photo_size);
				uart2_rx_wr=0;
			}
			else if(rt_tick_get()-tick>RT_TICK_PER_SECOND*2)	///�ж��Ƿ�ʱ
			{
				Current_Cam_Para.State=CAM_START;
			}
			break;
		}
	case CAM_END:
		{
			rt_kprintf("\r\n���ճɹ�!");
			getpicpara();
			
			Current_Cam_Para.State=CAM_IDLE;
			break;
		}
	case CAM_FALSE:
		{
			rt_kprintf("\r\n����ʧ��!");
			Current_Cam_Para.State=CAM_IDLE;
			break;
		}
	}
 return 1;
}


/*********************************************************************************
*��������:rt_err_t Cam_add_tx_pic_timeout( JT808_TX_MSG_NODEDATA * nodedata )
*��������:����ͼƬ������ݵĳ�ʱ������
*�� ��:	nodedata	:���ڴ���ķ�������
*�� ��: none
*�� �� ֵ:rt_err_t
*�� ��:������
*��������:2013-06-16
*---------------------------------------------------------------------------------
*�� �� ��:
*�޸�����:
*�޸�����:
*********************************************************************************/
static rt_err_t Cam_add_tx_pic_timeout( JT808_TX_MSG_NODEDATA * nodedata )
{
 rt_kprintf("\r\n Cam_tx_timeout");
}


/*********************************************************************************
*��������:u16 Cam_add_tx_pic_getdata( JT808_TX_MSG_NODEDATA * nodedata )
*��������:��jt808�д�����Ƭ���ݴ���ACK_ok�������ú����� JT808_TX_MSG_NODEDATA �� cb_tx_response �ص�����
*�� ��:	nodedata	:���ڴ���ķ�������
*�� ��: none
*�� �� ֵ:rt_err_t
*�� ��:������
*��������:2013-06-16
*---------------------------------------------------------------------------------
*�� �� ��:
*�޸�����:
*�޸�����:
*********************************************************************************/
static rt_err_t Cam_add_tx_pic_response( JT808_TX_MSG_NODEDATA * nodedata ,uint8_t linkno, uint8_t *pmsg )
{
	JT808_TX_MSG_NODEDATA	* iterdata=nodedata;
	TypePicMultTransPara	* p_para;
	uint16_t				i,pack_num;
	uint16_t				body_len; /*��Ϣ�峤��*/
	uint8_t					* msg;
	uint32_t				tempu32data;

	body_len	= ( ( *( pmsg + 2 ) << 8 ) | ( *( pmsg + 3 ) ) ) & 0x3FF;
	msg			= pmsg + 12;

	tempu32data=buf_to_data(msg,4);
	msg+=4;
	p_para = (TypePicMultTransPara*)(iterdata->user_para);
	if(tempu32data==p_para->Data_ID)
		{
		memset(p_para->Pack_Mark,0,sizeof(p_para->Pack_Mark));
		if(body_len>=7)
			{
			pack_num=*msg++;
			for(i=0;i<pack_num;i++)
				{
				tempu32data=buf_to_data(msg,2);
				if(tempu32data)
					tempu32data--;
				msg+=2;
				p_para->Pack_Mark[tempu32data/8] |= BIT(tempu32data%8);
				}
			iterdata->state=GET_DATA;
			rt_kprintf("\r\n Cam_add_tx_pic_response\r\n lost_pack=%d",pack_num);
			}
		else
			{
			iterdata->state=ACK_OK;
			if(p_para->Delete)
				{
				//Cam_Flash_DelPic(p_para->Data_ID);
				}
			rt_kprintf("\r\n Cam_add_tx_pic_response_ok!");
			}
		}
}


/*********************************************************************************
*��������:u16 Cam_add_tx_pic_getdata( JT808_TX_MSG_NODEDATA * nodedata )
*��������:��jt808_tx_proc��״̬ΪGET_DATAʱ��ȡ��Ƭ���ݣ��ú����� JT808_TX_MSG_NODEDATA �� get_data �ص�����
*�� ��:	nodedata	:���ڴ���ķ�������
*�� ��: none
*�� �� ֵ:rt_err_t
*�� ��:������
*��������:2013-06-16
*---------------------------------------------------------------------------------
*�� �� ��:
*�޸�����:
*�޸�����:
*********************************************************************************/
static u16 Cam_add_tx_pic_getdata( JT808_TX_MSG_NODEDATA * nodedata )
{
	JT808_TX_MSG_NODEDATA	* iterdata=nodedata;
	TypePicMultTransPara	* p_para = (TypePicMultTransPara *)nodedata->user_para;
	TypeDF_PackageHead 		TempPackageHead;
	uint16_t				i,wrlen,pack_num;
	uint16_t				body_len; /*��Ϣ�峤��*/
	uint8_t					* msg;
	uint32_t				tempu32data;

	//�ͷ�ԭ������Դ
	rt_free(iterdata->pmsg);
	iterdata->pmsg=RT_NULL;
	iterdata->msg_len = 0;
	//���ҵ�ǰͼƬ�Ƿ����
	tempu32data=Cam_Flash_FindPicID(p_para->Data_ID,&TempPackageHead);
	if(tempu32data == 0xFFFFFFFF)
		{
		return 0xFFFF;
		}
	for(i=0;i<iterdata->packet_num;i++)
		{
		if(p_para->Pack_Mark[i/8] & BIT(i%8))
			{
			if(i+1 < iterdata->packet_num)
				{
				body_len = JT808_PACKAGE_MAX;
				}
			else
				{
				body_len = iterdata->size - JT808_PACKAGE_MAX * i;
				}
			iterdata->pmsg = rt_malloc(body_len);
			iterdata->msg_len = body_len;
			if( RT_NULL == iterdata->pmsg )
				{
				rt_kprintf("\r\n Cam_add_tx_pic_getdata rt_malloc error!");
				return 0;
				}
			if(i==0)
				{
				sst25_read(tempu32data,(u8 *)&TempPackageHead,sizeof(TypeDF_PackageHead));
				iterdata->pmsg[0];
				wrlen=0;
				wrlen+=data_to_buf(iterdata->pmsg+wrlen,TempPackageHead.Data_ID,4);			///��ý��ID
				wrlen+=data_to_buf(iterdata->pmsg+wrlen,TempPackageHead.Media_Style,1);		///��ý������
				wrlen+=data_to_buf(iterdata->pmsg+wrlen,TempPackageHead.Media_Format,1);	///��ý���ʽ����
				wrlen+=data_to_buf(iterdata->pmsg+wrlen,TempPackageHead.TiggerStyle,1);		///�¼������ 
				wrlen+=data_to_buf(iterdata->pmsg+wrlen,TempPackageHead.Channel_ID,1);		///ͨ�� ID
				memcpy(iterdata->pmsg+wrlen,TempPackageHead.position,28);					///λ����Ϣ�㱨
				wrlen+=28;
				sst25_read(tempu32data+64,iterdata->pmsg+wrlen,body_len-wrlen);
				}
			else
				{
				tempu32data = tempu32data + JT808_PACKAGE_MAX * i + 64 - 36;
				sst25_read(tempu32data,iterdata->pmsg,body_len);
				}
			p_para->Pack_Mark[i/8] &= ~(BIT(i%8));
			iterdata->packet_no=i+1;
			rt_kprintf("\r\n cam_get_data ok\r\n PAGE=%d",iterdata->packet_no);
			return iterdata->packet_no;
			}
		}
	rt_kprintf("\r\n cam_get_data_false!");
	return 0;
}


/*********************************************************************************
*��������:rt_err_t Cam_add_tx_pic(u32 mdeia_id ,u8 media_delete)
*��������:���һ����ý��ͼƬ�������б���
*�� ��:	mdeia_id	:��Ƭid
		media_delete:��Ƭ���ͽ������Ƿ�ɾ����ǣ���0��ʾɾ��
*�� ��: none
*�� �� ֵ:rt_err_t
*�� ��:������
*��������:2013-06-16
*---------------------------------------------------------------------------------
*�� �� ��:
*�޸�����:
*�޸�����:
*********************************************************************************/
rt_err_t Cam_add_tx_pic(u32 mdeia_id ,u8 media_delete)
{
	u16 i;
	u32 TempAddress;
	JT808_TX_MSG_NODEDATA	* pnodedata;
	TypePicMultTransPara	* p_para;
	TypeDF_PackageHead TempPackageHead;

	
	TempAddress=Cam_Flash_FindPicID(mdeia_id,&TempPackageHead);
	if(TempAddress==0xFFFFFFFF)
		{
		return RT_ERROR;
		}
	p_para=rt_malloc( sizeof( TypePicMultTransPara ) );
	if( p_para == NULL )
		{
		return RT_ERROR;
		}
	memset(p_para,0,sizeof(TypePicMultTransPara));
	
	pnodedata = rt_malloc( sizeof( JT808_TX_MSG_NODEDATA ) );
	if( pnodedata == NULL )
		{
		rt_free(p_para);
		return RT_ERROR;
		}
	memset(pnodedata,0,sizeof(JT808_TX_MSG_NODEDATA));
	
	pnodedata->linkno			= 1;			
	pnodedata->multipacket		= 1;		//��ʾΪ�������
	pnodedata->type				= TERMINAL_CMD;
	pnodedata->state			= GET_DATA;
	pnodedata->retry			= 0;
	//pnodedata->max_retry		= 3;
	pnodedata->tick				= rt_tick_get();
	pnodedata->cb_tx_timeout	= Cam_add_tx_pic_timeout;
	pnodedata->cb_tx_response	= Cam_add_tx_pic_response;
	pnodedata->head_id			= 0x801;
	pnodedata->head_sn			= 0x00;

	pnodedata->size				= TempPackageHead.Len-64+36;
	pnodedata->packet_num		= (pnodedata->size/JT808_PACKAGE_MAX);
	if(pnodedata->size%JT808_PACKAGE_MAX)
		pnodedata->packet_num++;
	pnodedata->packet_no		= 0;

	///����û�������
	
	p_para->Address	= TempAddress;
	p_para->Data_ID	= mdeia_id;
	p_para->Delete	= media_delete;
	for(i=0;i<pnodedata->packet_num;i++)
		{
		p_para->Pack_Mark[i/8] |= BIT(i%8);
		}
	pnodedata->user_para		= (void *)p_para;
	pnodedata->get_data			= Cam_add_tx_pic_getdata;
	msglist_append( list_jt808_tx, pnodedata );
	rt_kprintf("\r\n Cam_add_tx_pic_ok!!!");
	return RT_EOK;
}

FINSH_FUNCTION_EXPORT( Cam_add_tx_pic, Cam_add_tx_pic);


/*********************************************************************************
*��������:void Cam_jt808_0x800(TypeDF_PackageHead *phead)
*��������:��ý���¼���Ϣ�ϴ�_������Ƭ��ý����Ϣ
*��	��:	phead	:��Ƭ��Ϣ��Ϣ
*��	��:	none
*�� �� ֵ:rt_err_t
*��	��:������
*��������:2013-06-16
*---------------------------------------------------------------------------------
*�� �� ��:
*�޸�����:
*�޸�����:
*********************************************************************************/
rt_err_t Cam_jt808_0x800(TypeDF_PackageHead *phead)
{
 u8 ptempbuf[32];
 u16 datalen=0;
 datalen+=data_to_buf(ptempbuf+datalen,phead->Data_ID,4);
 datalen+=data_to_buf(ptempbuf+datalen,phead->Media_Style,1);
 datalen+=data_to_buf(ptempbuf+datalen,phead->Media_Format,1);
 datalen+=data_to_buf(ptempbuf+datalen,phead->TiggerStyle,1);
 datalen+=data_to_buf(ptempbuf+datalen,phead->Channel_ID,1);
 return jt808_add_tx_data(1,TERMINAL_ACK,0x800,ptempbuf,datalen);
}

/*********************************************************************************
*��������:rt_err_t Cam_jt808_0x8802(uint16_t fram_num,uint8_t *pmsg,u16 msg_len)
*��������:�洢��ý�����ݼ��� 
*��	��:	fram_num:Ӧ����ˮ��
		pmsg	:808��Ϣ������
		msg_len	:808��Ϣ�峤��
*��	��:none
*�� �� ֵ:rt_err_t
*��	��:������
*��������:2013-06-16
*---------------------------------------------------------------------------------
*�� �� ��:
*�޸�����:
*�޸�����:
*********************************************************************************/
rt_err_t Cam_jt808_0x8802(uint16_t fram_num,uint8_t *pmsg,u16 msg_len)
{
 u8 *ptempbuf;
 u8 *pdestbuf;
 u16 datalen = 0;
 u16 i,mediatotal;
 TypeDF_PackageHead TempPackageHead;
 u32 TempAddress;
 rt_err_t ret;
 if(pmsg[0])
 	{
 	return RT_ERROR;
 	}
 if(DF_PicParameter.Number==0)
 	{
 	return RT_ERROR;
 	}
 
 ptempbuf = rt_malloc(DF_PicParameter.Number*4);
 if( ptempbuf == NULL )
	{
	return RT_ERROR;
	}
 
 memset(&TempPackageHead,0,sizeof(Style_Cam_Requset_Para));
 TempPackageHead.Media_Format	= 0;
 TempPackageHead.Media_Style	= 0;
 TempPackageHead.Channel_ID		= pmsg[1];
 TempPackageHead.TiggerStyle	= pmsg[2];
 ///���ҷ���������ͼƬ������ͼƬ��ַ����ptempbuf��
 mediatotal=Cam_Flash_SearchPic((T_TIMES *)(pmsg+3),(T_TIMES *)(pmsg+9),&TempPackageHead,ptempbuf);
 
 if(mediatotal > (JT808_PACKAGE_MAX-4)/35)
 	{
 	mediatotal = (JT808_PACKAGE_MAX-4)/35;
 	}
 
 pdestbuf = rt_malloc(mediatotal*35+4);
 if( pdestbuf == NULL )
	{
	rt_free(ptempbuf);
	return RT_ERROR;
	}
 
 datalen+=data_to_buf(pdestbuf+datalen,fram_num,2);
 datalen+=data_to_buf(pdestbuf+datalen,mediatotal,2);
 for(i=0;i<mediatotal;i++)
 	{
 	TempAddress=buf_to_data(ptempbuf,4);
	ptempbuf+=4;
	sst25_read(TempAddress,(u8 *)&TempPackageHead,sizeof(TempPackageHead));
	datalen+=data_to_buf(pdestbuf+datalen,TempPackageHead.Data_ID,4);
	datalen+=data_to_buf(pdestbuf+datalen,TempPackageHead.Media_Style,1);
	datalen+=data_to_buf(pdestbuf+datalen,TempPackageHead.Channel_ID ,1);
	datalen+=data_to_buf(pdestbuf+datalen,TempPackageHead.TiggerStyle,1);
	memcpy(pdestbuf+datalen,TempPackageHead.position,28);					///λ����Ϣ�㱨
	datalen+=28;
 	}
 ret = jt808_add_tx_data(1,TERMINAL_ACK,0x802,pdestbuf,datalen);
 
 rt_free(ptempbuf);
 rt_free(pdestbuf);
 return ret;
}


/*********************************************************************************
*��������:rt_err_t Cam_jt808_0x8803(uint8_t *pmsg,u16 msg_len)
*��������:�洢��ý�������ϴ�
*��	��:	pmsg	:808��Ϣ������
		msg_len	:808��Ϣ�峤��
*��	��:none
*�� �� ֵ:rt_err_t
*��	��:������
*��������:2013-06-16
*---------------------------------------------------------------------------------
*�� �� ��:
*�޸�����:
*�޸�����:
*********************************************************************************/
rt_err_t Cam_jt808_0x8803(uint8_t *pmsg,u16 msg_len)
{
 u8 media_delete = 0;
 u8 *ptempbuf;
 u16 datalen = 0;
 u16 i,mediatotal;
 TypeDF_PackageHead TempPackageHead;
 u32 TempAddress;
 rt_err_t ret;
 if(pmsg[0])
	{
	return RT_ERROR;
	}
 if(DF_PicParameter.Number==0)
	{
	return RT_ERROR;
	}
 
 ptempbuf = rt_malloc(DF_PicParameter.Number*4);
 if( ptempbuf == NULL )
	{
	return RT_ERROR;
	}
 
 memset(&TempPackageHead,0,sizeof(Style_Cam_Requset_Para));
 TempPackageHead.Media_Format	= 0;
 TempPackageHead.Media_Style	= 0;
 TempPackageHead.Channel_ID 	= pmsg[1];
 TempPackageHead.TiggerStyle	= pmsg[2];
 media_delete					= pmsg[15];
 ///���ҷ���������ͼƬ������ͼƬ��ַ����ptempbuf��
 mediatotal=Cam_Flash_SearchPic((T_TIMES *)(pmsg+3),(T_TIMES *)(pmsg+9),&TempPackageHead,ptempbuf);

 for(i=0;i<mediatotal;i++)
	{
	TempAddress=buf_to_data(ptempbuf,4);
	ptempbuf+=4;
	sst25_read(TempAddress,(u8 *)&TempPackageHead,sizeof(TempPackageHead));
	Cam_add_tx_pic(TempPackageHead.Data_ID ,media_delete);
	}
 rt_free(ptempbuf);
 return RT_EOK;
}
/************************************** The End Of File **************************************/
