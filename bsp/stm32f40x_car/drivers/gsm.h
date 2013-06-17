#ifndef _GSM_H_
#define _GSM_H_

/*
1.��gsm��װ��һ���豸���ڲ�gsm״̬����һ���̴߳���
  Ӧ�ó���ֻ��Ҫ���豸�����в������ɡ�

2.ͨ��AT�����ȡģ������? ��������ģ��Ͳ���Ҫˢ��
  ����Ҳ���ڱȽ�ģ�顣�ж��ʵ������?

3.ģ���״̬����Ϣ�����������·���Ϣ�����պ���/����
  ���߿���·��socket�Ͽ��� (��ͬ��ģ�鲻ͬ,Ҫ�����
  ��)�����֪ͨAPP? ʹ�ûص�������

4.��Ч��socket����
*/


#define GSM_UART_NAME	"uart4"

//#define MAX_SOCKETS	6	//EM310����3  MG323����6

/*
����ʹ�õ�ģ���ͺ�
*/
//#define MG323
#define M66
//#define MC323
//#define EM310



/*
GSM֧�ֲ������ܵ��б�
��ͬ��ģ��֧�ֵ����ͬ������¼������ TTS����
�˴�����ͳһ��һ��gsm.h,��Ӧ����ģ��Ĳ�ͬ����ͬ��
���ϲ�������ο���?

���ƽӿ�Ӧ��Ӧ�ðѹ��ֵܷ��㹻��ϸ
���ʵ��һ������:һ������/�������ƽ�������
*/
typedef enum
{
	CTL_STATUS=1,		//��ѯGSM״̬
	CTL_AT_CMD, 		//����AT����
	CTL_PPP,			//PPP����ά��
	CTL_SOCKET, 		//����socket
	CTL_DNS,			//����DNS����
	CTL_TXRX_COUNT, 	//���ͽ��յ��ֽ���
	CTL_CONNECT,		//ֱ�ӽ�������

}T_GSM_CONTROL_CMD;




typedef enum
{
	GSM_STATE=0,		/*��ѯGSM״̬*/
	GSM_IDLE=1,			/*����*/
	GSM_POWERON,		/*�ϵ���̲����ģ���AT�����ʼ������*/
	GSM_POWEROFF,		/*�Ѿ��ϵ�*/
	GSM_AT,				/*����AT�����շ�״̬,����socket�������շ�����*/
	GSM_GPRS,		/*��¼GPRS��*/
	GSM_TCPIP,			/*�Ѿ����������Խ���socket����*/
	GSM_SOCKET_PROC,	/*���ڽ���socket����*/
	GSM_ERR_POWERON,
	GSM_ERR_GPRS,
	GSM_ERR_TCPIP,
}T_GSM_STATE;


typedef enum
{
	SOCKET_STATE=0,		/*��ѯsocket״̬*/
	SOCKET_IDLE = 1,            /*��������*/
	SOCKET_ERR,
	SOCKET_START,				/*��������Զ��*/
	SOCKET_DNS,                 /*DNS��ѯ��*/
	SOCKET_DNS_ERR,
	SOCKET_CONNECT,             /*������*/
	SOCKET_CONNECT_ERR,         /*���Ӵ��󣬶Է���Ӧ��*/
	SOCKET_READY,               /*����ɣ����Խ�������*/
	SOCKET_CLOSE,
}T_SOCKET_STATE;

/*���֧��4������*/
#define MAX_GSM_SOCKET 4

typedef struct
{
	T_SOCKET_STATE	state;          /*����״̬*/
	uint8_t			linkno;			/*��ʹ�õ�link��*/
	char			type;           /*�������� 'u':udp client 't':TCP client  'U' udp server*/
	char			ipstr[64];    /*�������ַ*/
	char			ip_addr[16];     /*dns���IP xxx.xxx.xxx.xxx*/
	uint16_t		port;           /*�˿�*/
	//MsgList			* msglist_tx;
}GSM_SOCKET;


void gsm_init(void);

void ctl_gprs( char* apn, char* user, char*psw ,uint8_t fdial );

void ctl_socket( uint8_t linkno,char type, char* remoteip, uint16_t remoteport,uint8_t fconnect );

#endif