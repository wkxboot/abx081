#include "FreeRTOS.h"
#include "task.h"
#include "cmsis_os.h"
#include "app_common.h"
#include "eeprom.h"
#include "string.h"
#include "json.h"
#include "service.h"
#include "report_task.h"
#include "shopping_task.h"
#include "lock_ctrl_task.h"

#define APP_LOG_MODULE_NAME   "[shopping]"
#define APP_LOG_MODULE_LEVEL   APP_LOG_LEVEL_DEBUG    
#include "app_log.h"
#include "app_error.h"


osThreadId shopping_task_hdl;

static http_request_t shopping_request;
static http_response_t shopping_response;

json_pull_open_instruction_t pull_open;
json_report_open_status_t    report_open;
json_report_close_status_t   report_close;
json_report_device_status_t  report_device;

extern info_head_t info_head;

static void shopping_task_init()
{
/*拉取开门指令json*/
pull_open.header.item_cnt=2;
json_set_item_name_value(&pull_open.pid,"\"pid\"",EXPERIMENT_IMEI); /*测试用 imei */
json_set_item_name_value(&pull_open.version,"\"version\"",FIRMWARE_VERSION); 
/*开门上报json*/
report_open.header.item_cnt=5;
json_set_item_name_value(&report_open.pid,"\"pid\"",EXPERIMENT_IMEI); 
json_set_item_name_value(&report_open.version,"\"version\"",FIRMWARE_VERSION);  
json_set_item_name_value(&report_open.open,"\"open\"",NULL); 
json_set_item_name_value(&report_open.open_uuid,"\"openUuid\"",NULL); 
json_set_item_name_value(&report_open.error,"\"error\"",NULL);  
/*关门上报json*/
report_close.header.item_cnt=6;
json_set_item_name_value(&report_close.pid,"\"pid\"",EXPERIMENT_IMEI); 
json_set_item_name_value(&report_close.version,"\"version\"",FIRMWARE_VERSION); 
json_set_item_name_value(&report_close.user_pin,"\"userPin\"",NULL); 
json_set_item_name_value(&report_close.open_uuid,"\"openUuid\"",NULL);  
json_set_item_name_value(&report_close.type,"\"type\"",NULL); 
json_set_item_name_value(&report_close.auto_lock,"\"autoLock\"",NULL); 
/*设备状态上报json*/
report_device.header.item_cnt=3;
json_set_item_name_value(&report_device.pid,"\"pid\"",EXPERIMENT_IMEI); 
json_set_item_name_value(&report_device.type,"\"type\"","2"); 
json_set_item_name_value(&report_device.log,"\"log\"",NULL); 
report_device.log.value[0]=10;
report_device.log.type=JSON_TYPE_NEST_STR;
json_set_item_name_value(&report_device.version,"\\\"version\\\"",FIRMWARE_VERSION_EX);    
json_set_item_name_value(&report_device.ip,"\\\"ip\\\"",EXPERIMENT_IP_EX);  
json_set_item_name_value(&report_device.m_power,"\\\"mPower\\\"","1"); /*主电源状态*/
json_set_item_name_value(&report_device.e_power,"\\\"ePower\\\"","1"); /*备用电源状态*/ 
json_set_item_name_value(&report_device.lock,"\\\"lock\\\"","1");  /*锁状态*/ 
json_set_item_name_value(&report_device.net,"\\\"net\\\"",EXPERIMENT_NET);  
json_set_item_name_value(&report_device.rssi,"\\\"rssi\\\"",EXPERIMENT_RSSI); 
json_set_item_name_value(&report_device.push_id,"\\\"pushId\\\"",EXPERIMENT_IMEI_EX); 
json_set_item_name_value(&report_device.boot,"\\\"boot\\\"","1");  /*启动状态*/ 
json_set_item_name_value(&report_device.temperature,"\\\"temperature\\\"","12"); /*温度*/   
}


/*测试代码*/
//uint8_t emulate[]="{\"result\":{\"expire\":\"1521080916800\",\"token\":\"e76aebcfa6096a70994d69e5811430c3d3836a4a\",\"data\":{\"userPin\":\"JD_20874f3ca9d474f\",\"type\":0},\"uuid\":\"30bbf00e09664194b0d2442a0210dace\"},\"code\":\"0\",\"msg\":\"成功\"}";
/*
static uint8_t str[300];
static uint8_t ip[]="\"12.23.34.56\"";
*/
/*购物流程任务*/
void shopping_task(void const * argument)
{
 osEvent sig;
 app_bool_t result;
 uint8_t param_size;
 json_item_t item;
 app_bool_t lock_success;
 APP_LOG_INFO("@购货任务开始.\r\n");
 
 shopping_task_init();
/*测试代码*/
/*
 while(1)
 {
  service_cpy_ip_str_ex(report_device.ip.value,ip);
  service_cpy_imei_str_to(report_device.pid.value);
  service_cpy_imei_str_to_ex(report_device.push_id.value);
  json_body_to_str_ex(&report_device,str);
  APP_LOG_DEBUG("str:\r\n%s\r\n",str);
  osDelay(1000);
 }
*/
 
 APP_LOG_DEBUG("购物任务初始化成功.\r\n");
 APP_LOG_DEBUG("购物任务等待同步完成...\r\n");
 /*等待任务同步*/
 xEventGroupSync(task_sync_evt_group_hdl,SHOPPING_TASK_SYNC_EVT,SHOPPING_TASK_SYNC_EVT|REPORT_TASK_SYNC_EVT,osWaitForever); 
 APP_LOG_DEBUG("购物任务同步完成.\r\n");
 
 /*如果在产品模式拷贝需要的imei*/
 if(SERVICE_MODE==SERVICE_MODE_IN_PRODUCTION)
 {
 service_cpy_imei_str_to(pull_open.pid.value);
 service_cpy_imei_str_to(report_open.pid.value);
 service_cpy_imei_str_to(report_close.pid.value);
 }
 
 /*上电等待关门*/
  while(1)
  {
  sig=osSignalWait(SHOPPING_TASK_ALL_SIGNALS,osWaitForever);
  /*关门成功*/
  if(sig.status==osEventSignal)
  {
   if(sig.value.signals & SHOPPING_TASK_AUTO_LOCK_LOCK_SUCCESS_SIGNAL)
   {
   APP_LOG_DEBUG("购物任务收到自动关门信号.\r\n");
   json_set_item_name_value(&report_close.auto_lock,NULL,"0");
   break;
   }
   if(sig.value.signals & SHOPPING_TASK_MAN_LOCK_LOCK_SUCCESS_SIGNAL)
   {
   APP_LOG_DEBUG("购物任务收到手动关门信号.\r\n");
   json_set_item_name_value(&report_close.auto_lock,NULL,"1");
   break;
   }
  }
  } 
 /*首先查看是否保存未上报信息 如果有未上报*/
  /*
 result=eeprom_read_unreport_close_info_head(&info_head);

 if(result==APP_TRUE)
 {
  if(info_head.valid==APP_TRUE)
  {
  APP_LOG_WARNING("存在未上报信息.\r\n");
  goto report_close;  
  }
 }
 APP_LOG_WARNING("不存在未上报信息.流程继续...\r\n");
*/
 while(1)
 {
  osDelay(SHOPPING_TASK_INTERVAL); 
 
  /*拉取开门指令*/
  shopping_request.ptr_url=PULL_OPEN_URL;
  if(json_body_to_str(&pull_open,shopping_request.param)!=APP_TRUE)
  {
  APP_LOG_ERROR("pull open param err.\r\n");
  }
  param_size=strlen((const char *)shopping_request.param);
  service_http_make_request_size_time_to_str(param_size,SHOPPING_TASK_DOWNLOAD_TIMEOUT,shopping_request.size_time);
  while(1)
  {
  result=service_http_post(&shopping_request,&shopping_response,HTTP_RESPONSE_TIMEOUT);
  if(result==APP_TRUE)
  {
  json_set_item_name_value(&item,"code",NULL);
  json_get_item_value_by_name_from_json_str(shopping_response.json_str,item.name,item.value); 
  if(strcmp((const char *)item.value,"\"0\"")==0)
  break;
  }
  APP_LOG_ERROR("拉取开门命令失败.\r\n");
  osDelay(SHOPPING_TASK_RETRY_TIMEOUT);
  }
  APP_LOG_DEBUG("拉取开门命令成功.\r\n");
  /*查看是否有开门uuid*/
  json_set_item_name_value(&item,"uuid",NULL);
  result=json_get_item_value_by_name_from_json_str(shopping_response.json_str,item.name,item.value); 
  /*如果没有找到uuid*/
  if(result!=APP_TRUE)
  {
  APP_LOG_INFO("没有开门指令.继续请求...\r\n");
  continue;
  }
  
  //strcpy((char *)shopping_response.json_str,(const char *)emulate);
  APP_LOG_INFO("收到开门指令.\r\n");
  APP_LOG_INFO("拷贝开门指令中需要的信息...\r\n");
  /*拷贝json中uuid的值到report open中的open_uuid*/
  result=json_get_item_value_by_name_from_json_str(shopping_response.json_str,"uuid",report_open.open_uuid.value);
  /*拷贝json中uuid的值到report close中的open_uuid*/
  result=json_get_item_value_by_name_from_json_str(shopping_response.json_str,"uuid",report_close.open_uuid.value);
  /*拷贝json中userpin的值到report close中的userpin*/
  result=json_get_item_value_by_name_from_json_str(shopping_response.json_str,report_close.user_pin.name,report_close.user_pin.value);
  /*拷贝json中type的值到report close中的type*/
  result=json_get_item_value_by_name_from_json_str(shopping_response.json_str,report_close.type.name,report_close.type.value);
  
  /*清除所有信号*/
  osSignalWait(SHOPPING_TASK_ALL_SIGNALS,0);
  /*操作开锁*/
  osSignalSet(lock_ctrl_task_hdl,LOCK_CTRL_TASK_UNLOCK_SIGNAL);
  /*等待完成操作*/
  sig=osSignalWait(SHOPPING_TASK_ALL_SIGNALS,osWaitForever);
  /*开锁成功*/
  if(sig.status==osEventSignal && sig.value.signals & SHOPPING_TASK_UNLOCK_LOCK_SUCCESS_SIGNAL)
  {
  json_set_item_name_value(&report_open.open,NULL,"true");
  json_set_item_name_value(&report_open.error,NULL,"0");
  lock_success=APP_TRUE;
  APP_LOG_DEBUG("开锁成功.\r\n");
  }
  else
  {
  json_set_item_name_value(&report_open.open,NULL,"false");
  json_set_item_name_value(&report_open.error,NULL,"4");
  lock_success=APP_FALSE;
  APP_LOG_DEBUG("开锁失败.\r\n");  
  }
  
 /*上报开锁状态*/
  shopping_request.ptr_url=REPORT_OPEN_URL;
  if(json_body_to_str(&report_open,shopping_request.param)!=APP_TRUE)
  {
  APP_LOG_ERROR("report open param err.\r\n");
  }
  param_size=strlen((const char *)shopping_request.param);
  service_http_make_request_size_time_to_str(param_size,SHOPPING_TASK_DOWNLOAD_TIMEOUT,shopping_request.size_time);
  while(1)
  {
  result=service_http_post(&shopping_request,&shopping_response,HTTP_RESPONSE_TIMEOUT);
  if(result==APP_TRUE)
  {
  json_set_item_name_value(&item,"code",NULL);
  json_get_item_value_by_name_from_json_str(shopping_response.json_str,item.name,item.value); 
  if(strcmp((const char *)item.value,"\"0\"")==0)
  break;
  }
  APP_LOG_ERROR("上报开锁状态失败.\r\n");
  osDelay(SHOPPING_TASK_RETRY_TIMEOUT);
  } 
  APP_LOG_INFO("上报开锁状态成功.\r\n");
  
  if(lock_success==APP_FALSE)
  continue;
  /*
  APP_LOG_DEBUG("购物任务保存上报信息.\r\n");
  info_head.valid=APP_TRUE;
  info_head.open_uuid_str_cnt=strlen((const char *)report_close.open_uuid.value)+1;
  info_head.user_pin_str_cnt=strlen((const char *)report_close.user_pin.value)+1;
  info_head.type_str_cnt=strlen((const char *)report_close.type.value)+1;
   
  eeprom_write_unreport_close_info(&info_head,report_close.open_uuid.value,report_close.user_pin.value,report_close.type.value);
  eeprom_write_unreport_close_info_head(&info_head);
  */
  /*服务器回应code:"0"*/
  /*等待关门*/
  while(1)
  {
  sig=osSignalWait(SHOPPING_TASK_ALL_SIGNALS,osWaitForever);
  /*关门成功*/
  if(sig.status==osEventSignal)
  {
   if(sig.value.signals & SHOPPING_TASK_AUTO_LOCK_LOCK_SUCCESS_SIGNAL)
   {
   APP_LOG_DEBUG("购物任务收到自动关门信号.\r\n");
   json_set_item_name_value(&report_close.auto_lock,NULL,"0");
   break;
   }
   if(sig.value.signals & SHOPPING_TASK_MAN_LOCK_LOCK_SUCCESS_SIGNAL)
   {
   APP_LOG_DEBUG("购物任务收到手动关门信号.\r\n");
   json_set_item_name_value(&report_close.auto_lock,NULL,"1");
   break;
   }
   if(sig.value.signals & SHOPPING_TASK_UPS_PWR_ON_SIGNAL)
   {
   APP_LOG_DEBUG("购物任务收到UPS接通市电信号.\r\n");
   }
   if(sig.value.signals & SHOPPING_TASK_UPS_PWR_OFF_SIGNAL)
   {
   APP_LOG_DEBUG("购物任务收到UPS断开市电信号.\r\n");
   }
   }
  }
 //report_close:  
  /*上报关门状态*/  
  shopping_request.ptr_url=REPORT_CLOSE_URL;
  if(json_body_to_str(&report_close,shopping_request.param)!=APP_TRUE)
  {
  APP_LOG_ERROR("report close param err.\r\n");
  }
  param_size=strlen((const char *)shopping_request.param);
  service_http_make_request_size_time_to_str(param_size,SHOPPING_TASK_DOWNLOAD_TIMEOUT,shopping_request.size_time);
  while(1)
  {
  result=service_http_post(&shopping_request,&shopping_response,HTTP_RESPONSE_TIMEOUT);
  if(result==APP_TRUE)
  {
  json_set_item_name_value(&item,"code",NULL);
  json_get_item_value_by_name_from_json_str(shopping_response.json_str,item.name,item.value); 
  /*服务器回应code:"0"*/ 
  if(strcmp((const char *)item.value,"\"0\"")==0)
  break;
  }
  APP_LOG_ERROR("上报关门状态失败.\r\n");
  osDelay(SHOPPING_TASK_RETRY_TIMEOUT);
  }
  APP_LOG_INFO("上报关门状态成功.\r\n");
  /*
  APP_LOG_DEBUG("购物任务无效上报信息.\r\n");
  info_head.valid=APP_FALSE;
  eeprom_write_unreport_close_info_head(&info_head);
*/
 }
 }
