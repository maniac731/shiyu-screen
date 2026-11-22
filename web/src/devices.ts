import type {SupabaseClient} from '@supabase/supabase-js';
const store='shiyu.pending-device-bind';
let pending:string|null=null;
try {
 const raw=new URLSearchParams(location.hash.slice(1)).get('device-bind');
 if(raw&&/^[a-f0-9]{64}$/i.test(raw)){pending=raw.toLowerCase();sessionStorage.setItem(store,pending);}
 else {const saved=sessionStorage.getItem(store);if(saved&&/^[a-f0-9]{64}$/.test(saved))pending=saved;}
}catch{/* memory-only flow */}
if(location.hash.includes('device-bind='))history.replaceState(null,'',location.pathname+location.search);
const item=(tag:string,text='')=>{const n=document.createElement(tag);n.textContent=text;return n;};
export function mountDevices(host:HTMLElement,client:SupabaseClient,isOwner:boolean,isCurrent:()=>boolean){
 let busy=false;
 const status=item('p');status.setAttribute('role','status');host.append(item('h3','屏幕'),status);
 async function call(action:string,extra:Record<string,string>={}){
  const {data,error}=await client.functions.invoke('device-owner',{body:{action,...extra}});
  if(error||data?.error)throw new Error('设备接口暂不可用，请检查部署、权限或二维码有效期。');return data;
 }
 function button(text:string,fn:()=>Promise<void>){const b=document.createElement('button');b.type='button';b.textContent=text;
  b.onclick=()=>{if(busy||!isCurrent())return;busy=true;b.disabled=true;void fn().catch(e=>{if(isCurrent())status.textContent=e.message;}).finally(()=>{busy=false;if(isCurrent())b.disabled=false;});};return b;}
 async function render(){
  if(!isOwner){status.textContent='屏幕由厨房创建者绑定。加入厨房的家人无需重复扫码。';return;}
  if(pending){const claim=pending;const data=await call('preview',{claim_token:claim});if(!isCurrent()||pending!==claim)return;
   const panel=item('div');host.append(panel);
   panel.append(item('p',`将屏幕 ${data.device_id} 绑定到「${data.household_name}」？`),button('确认绑定屏幕',async()=>{
    if(pending!==claim)return;
    await call('claim',{claim_token:claim});if(!isCurrent())return;pending=null;sessionStorage.removeItem(store);panel.remove();status.textContent='绑定成功，屏幕联网后会自动同步。';await list();
   }),button('取消本次绑定',async()=>{pending=null;sessionStorage.removeItem(store);panel.remove();if(isCurrent())status.textContent='已取消，可重新扫描屏幕二维码。';}));
  }
  await list();
 }
 async function list(){const data=await call('list');if(!isCurrent())return;host.querySelector('.device-list')?.remove();const listNode=item('div');listNode.className='device-list';
  for(const d of data.devices??[]){const row=item('div');row.append(item('p',`屏幕 ${d.id}${d.revoked_at?'（已停用）':''}`));
   if(!d.revoked_at)row.append(button('停用屏幕同步',async()=>{if(!confirm('停止此屏幕的云端访问？已下载的本地照片仍会保留。'))return;await call('revoke',{device_id:d.id});if(isCurrent()){status.textContent='已停用。屏幕需联网后得知，本地照片不会自动清空。';await list();}}));listNode.append(row);
  }host.append(listNode);
 }
 void render().catch(e=>{if(isCurrent())status.textContent=e.message;});
}
