import {admin,body,hash,randomToken,reply,origin,token,uuid} from '../_shared/devices.ts';
Deno.serve(async req=>{
 if(req.method==='OPTIONS')return reply({});
 try {
  const b=await body(req); const id=req.headers.get('x-device-id')??'', secret=req.headers.get('x-device-secret')??'';
  if(!uuid(id)||!token(secret)||!['enroll','pairing','status','manifest','album','photo_url'].includes(b.action))return reply({error:'invalid request'},400);
  const client=admin();
  if(b.action==='album'||b.action==='photo_url'){
   if(b.action==='photo_url'&&!uuid(b.photo_id??''))return reply({error:'invalid photo'},400);
   const {data,error}=await client.rpc('device_album',{p_device:id,p_hash:await hash(secret),p_photo:b.action==='photo_url'?b.photo_id:null});
   if(error)return reply({error:'device or photo unavailable'},403);
   if(b.action==='album')return reply(data);
   const {data:signed,error:signError}=await client.storage.from('kitchen-photos').createSignedUrl(data.object_path,120);
   if(signError||!signed)return reply({error:'photo unavailable'},503);
   return reply({id:data.id,download_url:signed.signedUrl});
  }
  const claim=b.action==='pairing'?randomToken():null;
  const {data,error}=await client.rpc('device_control',{p_action:b.action,p_device:id,p_hash:await hash(secret),p_claim:claim?await hash(claim):null});
  if(error)return reply({error:error.code==='54000'?'retry later':'device unavailable'},error.code==='54000'?429:403);
  if(claim)return reply({...data,claim_token:claim,bind_url:`${origin}/#device-bind=${claim}`});
  if(b.action==='manifest'&&data.state==='photo'){
   if(data.photo.id===b.known_photo_id)return reply({state:'unchanged'});
   const {data:signed,error:signError}=await client.storage.from('kitchen-photos').createSignedUrl(data.photo.object_path,120);
   if(signError||!signed)return reply({error:'photo unavailable'},503);
   return reply({state:'photo',photo:{id:data.photo.id,download_url:signed.signedUrl,expires_at:new Date(Date.now()+120000).toISOString(),max_bytes:1048576,width:1024,height:600}});
  }
  return reply(data);
 }catch{return reply({error:'invalid request'},400);}
});
