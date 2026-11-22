import { createClient } from 'npm:@supabase/supabase-js@2.89.0';
export const origin = Deno.env.get('WEB_ORIGIN') ?? 'https://kitchen.maniac3221225477.sbs';
export const admin = () => createClient(Deno.env.get('SUPABASE_URL')!, Deno.env.get('SUPABASE_SERVICE_ROLE_KEY')!, { auth: { persistSession: false, autoRefreshToken: false } });
export const uuid = (s: string) => /^[0-9a-f]{8}-[0-9a-f]{4}-[0-9a-f]{4}-[0-9a-f]{4}-[0-9a-f]{12}$/i.test(s);
export const token = (s: string) => /^[0-9a-f]{64}$/i.test(s);
export async function hash(s: string) { return [...new Uint8Array(await crypto.subtle.digest('SHA-256',new TextEncoder().encode(s)))].map(x=>x.toString(16).padStart(2,'0')).join(''); }
export function randomToken() { return [...crypto.getRandomValues(new Uint8Array(32))].map(x=>x.toString(16).padStart(2,'0')).join(''); }
export function reply(data: unknown, status=200) { return new Response(JSON.stringify(data), {status, headers:{'Content-Type':'application/json','Cache-Control':'no-store','Access-Control-Allow-Origin':origin,'Vary':'Origin','Access-Control-Allow-Headers':'authorization, apikey, content-type, x-client-info, x-device-id, x-device-secret','Access-Control-Allow-Methods':'POST, OPTIONS'}}); }
export async function body(req: Request) {
 if(req.headers.has('origin') && req.headers.get('origin')!==origin) throw new Error('origin');
 if(req.method!=='POST') throw new Error('method');
 // Bound actual bytes, not just the optional Content-Length header.
 const reader=req.body?.getReader(); if(!reader) throw new Error('body');
 let size=0; const chunks:Uint8Array[]=[];
 try {while(true){const {done,value}=await reader.read(); if(done) break; size+=value.length; if(size>2048) throw new Error('body'); chunks.push(value);}}
 finally {await reader.cancel().catch(()=>{});reader.releaseLock();}
 const all=new Uint8Array(size);let offset=0;for(const c of chunks){all.set(c,offset);offset+=c.length;}
 const parsed=JSON.parse(new TextDecoder().decode(all));
 if(!parsed || typeof parsed!=='object' || Array.isArray(parsed)) throw new Error('body'); return parsed;
}
