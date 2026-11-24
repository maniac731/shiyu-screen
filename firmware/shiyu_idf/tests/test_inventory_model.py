#!/usr/bin/env python3
"""Host regression checks; requires Python 3 and a C compiler, no ESP-IDF/device."""
from pathlib import Path
import subprocess
import tempfile
import sys
# All generated headers, executable and cache fixtures stay in a temporary directory.
_temp = tempfile.TemporaryDirectory(prefix='shiyu-inventory-test-')
root=Path(_temp.name)
files={'cJSON.h':'''typedef struct cJSON {char *valuestring; struct cJSON *next,*child;} cJSON;
cJSON *cJSON_GetObjectItemCaseSensitive(const cJSON*,const char*); int cJSON_IsString(const cJSON*); int cJSON_IsArray(const cJSON*);int cJSON_IsNull(const cJSON*);int cJSON_GetArraySize(const cJSON*);
#define cJSON_ArrayForEach(e,a) for(e=(a)->child;e;e=e->next)
''','esp_heap_caps.h':'''#include <stdlib.h>
#define MALLOC_CAP_SPIRAM 1
#define MALLOC_CAP_8BIT 2
#define heap_caps_malloc(n,c) malloc(n)
#define heap_caps_calloc(n,s,c) calloc(n,s)
''','esp_log.h':'#define ESP_LOGI(...) ((void)0)\n','freertos/FreeRTOS.h':'#define portMAX_DELAY 0\n','freertos/semphr.h':'''typedef int SemaphoreHandle_t;
#define xSemaphoreTake(...) ((void)0)
#define xSemaphoreGive(...) ((void)0)
#define xSemaphoreCreateMutex() 1
''','nvs.h':'''typedef int nvs_handle_t; typedef int esp_err_t;
#define ESP_OK 0
#define NVS_READONLY 0
#define NVS_READWRITE 1
int nvs_open(const char*,int,nvs_handle_t*);int nvs_get_u32(nvs_handle_t,const char*,unsigned*);int nvs_set_u32(nvs_handle_t,const char*,unsigned);int nvs_commit(nvs_handle_t);void nvs_close(nvs_handle_t);
'''}
for name,content in files.items():
    target=root/name
    target.parent.mkdir(parents=True,exist_ok=True)
    target.write_text(content)
source=Path(__file__).resolve().parents[1]/'main'/'inventory_model.c'
(root/'test.c').write_text('''#include <assert.h>
#include "'''+str(source)+'''"
int main(int argc,char **argv) {
 assert(date_valid("2024-02-29")&&!date_valid("2025-02-29")&&!date_valid("2026-13-01")&&!date_valid("2026-01-00")&&date_valid(""));
 s_data=calloc(1,sizeof(*s_data));s_available=true;s_data->count=4;
 inventory_row_t *r=s_data->rows;
 strcpy(r[0].name,"Apple");strcpy(r[0].unit,"kg");strcpy(r[0].quantity,"999999.999");strcpy(r[0].expires_on,"2026-12-02");
 r[1]=r[0];strcpy(r[1].name,"apple");strcpy(r[1].quantity,"0.002");strcpy(r[1].expires_on,"2026-12-01");
 r[2]=r[0];strcpy(r[2].name,"pear");r[2].expires_on[0]=0;
 r[3]=r[0];strcpy(r[3].name,"banana");strcpy(r[3].expires_on,"2026-11-30");
 inventory_row_t out[4];assert(grouped(out,0,4)==3);assert(!strcmp(out[0].name,"banana"));assert(out[1].batches==2);assert(!strcmp(out[1].quantity,"1000000.001"));assert(!strcmp(out[2].name,"pear"));
 /* Delete the representative dated batch; stable group key still reads surviving batch. */
 r[1]=r[3];s_data->count=3;
 assert(inventory_model_group_batches("apple","kg",out,4)==1);
 assert(!strcmp(out[0].name,"Apple")&&!strcmp(out[0].quantity,"999999.999"));
 s_data->count=100;
 for(unsigned i=1;i<100;i++) r[i]=r[0];
 assert(grouped(out,0,4)==1&&out[0].batches==100&&!strcmp(out[0].quantity,"99999999.900"));
 inventory_row_t *all=calloc(100,sizeof(*all));
 assert(inventory_model_group_batches("apple","kg",all,100)==100);free(all);
 legacy_snapshot_t old={.schema=1,.count=1,.generation=7};strcpy(old.device,"11111111-1111-1111-1111-111111111111");strcpy(old.household,old.device);memset(old.revision,'a',64);
 strcpy(old.rows[0].id,old.device);strcpy(old.rows[0].name,"legacy");strcpy(old.rows[0].quantity,"1.250");strcpy(old.rows[0].unit,"kg");
 uint32_t h=2166136261U;unsigned char *bytes=(void*)&old;for(size_t i=0;i<offsetof(legacy_snapshot_t,checksum);i++)h=(h^bytes[i])*16777619U;old.checksum=h;
 assert(argc==2);paths[0]=argv[1];FILE*f=fopen(paths[0],"wb");assert(f);assert(fwrite(&old,1,sizeof(old),f)==sizeof(old));fclose(f);
 snapshot_t *loaded=calloc(1,sizeof(*loaded));assert(read_slot(0,loaded));assert(loaded->schema==2&&loaded->generation==7&&loaded->count==1&&!strcmp(loaded->rows[0].quantity,"1.250")&&!loaded->rows[0].expires_on[0]);
 puts("PASS dates, aggregation, milli totals, sorting, schema1 cache migration");return 0;
}
''')
subprocess.run(['cc','-I'+str(root),'-ffunction-sections','-fdata-sections',str(root/'test.c'),*(['-Wl,-dead_strip'] if sys.platform=='darwin' else ['-Wl,--gc-sections']),'-lm','-o',str(root/'test')],check=True)
subprocess.run([str(root/'test'),str(root/'legacy.bin')],check=True)
_temp.cleanup()
