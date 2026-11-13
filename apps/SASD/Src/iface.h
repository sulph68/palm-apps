#ifndef _SASD_IFACE_H_
#define _SASD_IFACE_H_


#define SASD_API_CREATOR				'sasd'
#define SASD_PREFS_CREATOR				'PpSS'

//calls
#define SASD_VERIFY						0x0000		//data is u8, size is 1, driver will invert it to prove it is the SASD driver
#define SASD_GET_VERS					0x0001		//data is out, struct SdVersionInfo
#define SASD_BATTERY_LVL				0x0002		//data is out, struct SdBatteryData
#define SASD_FW_UPDATE_START			0x0003		//no data
#define SASD_GET_IFACE_SPEED			0x0004		//data is out U32
#define SASD_GET_FLAGS					0x0005		//data is U32 mask of SD_FLAG_*


#endif

