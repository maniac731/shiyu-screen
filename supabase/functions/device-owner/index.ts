import {admin,body,hash,reply,token,uuid} from '../_shared/devices.ts';
Deno.serve(async req=>{
 if(req.method==='OPTIONS')return reply({});
 try {
  const b=await body(req), bearer=req.headers.get('authorization')??'';
  if(!bearer.startsWith('Bearer '))return reply({error:'login required'},401);
  const client=admin();const {data:auth,error:authError}=await client.auth.getUser(bearer.slice(7));
  if(authError||!auth.user)return reply({error:'login required'},401);
  if(!['preview','claim','revoke','list'].includes(b.action))return reply({error:'invalid action'},400);
  if(['preview','claim'].includes(b.action)&&!token(b.claim_token??''))return reply({error:'pairing unavailable'},400);
  if(b.action==='revoke'&&!uuid(b.device_id??''))return reply({error:'device unavailable'},400);
  const {data,error}=await client.rpc('device_control',{p_action:b.action,p_user:auth.user.id,p_claim:b.claim_token?await hash(b.claim_token.toLowerCase()):null,p_device:b.device_id??null});
  return error?reply({error:'device or pairing unavailable'},403):reply(data);
 }catch{return reply({error:'invalid request'},400);}
});
