// replacement loadPNG: 8-bit, colour type 2 or 6, interlaced (Adam7) or not
const fs=require("fs"), zlib=require("zlib");
function unfilter(raw, p, passW, passH, ch){
  const stride=passW*ch, out=Buffer.alloc(stride*passH);
  for(let y=0;y<passH;y++){
    const f=raw[p++]; const line=raw.slice(p,p+stride); p+=stride;
    for(let x=0;x<stride;x++){
      const a = x>=ch ? out[y*stride+x-ch] : 0;
      const b = y>0 ? out[(y-1)*stride+x] : 0;
      const c = (x>=ch && y>0) ? out[(y-1)*stride+x-ch] : 0;
      let v=line[x];
      if(f===1) v+=a; else if(f===2) v+=b; else if(f===3) v+=(a+b)>>1;
      else if(f===4){ const pa=Math.abs(b-c), pb=Math.abs(a-c), pc=Math.abs(a+b-2*c);
                      v += (pa<=pb&&pa<=pc)?a:(pb<=pc?b:c); }
      out[y*stride+x]=v&255;
    }
  }
  return {out, next:p};
}
function loadPNG(path){
  const b=fs.readFileSync(path);
  const w=b.readUInt32BE(16), h=b.readUInt32BE(20), depth=b[24], ct=b[25], interlace=b[28];
  const ch = ct===6?4:ct===2?3:0;
  if(!ch || depth!==8) throw new Error(`png: colour type ${ct} depth ${depth} not handled`);
  let idat=[], o=8;
  while(o<b.length){
    const len=b.readUInt32BE(o), type=b.toString("latin1",o+4,o+8);
    if(type==="IDAT") idat.push(b.slice(o+8,o+8+len));
    o += 12+len;
    if(type==="IEND") break;
  }
  const raw=zlib.inflateSync(Buffer.concat(idat));
  const data=Buffer.alloc(w*h*ch), stride=w*ch;
  if(!interlace){
    const {out}=unfilter(raw,0,w,h,ch);
    out.copy(data);
    return {w,h,ch,data};
  }
  // Adam7: seven passes, each filtered on its own width
  const P=[[0,0,8,8],[4,0,8,8],[0,4,4,8],[2,0,4,4],[0,2,2,4],[1,0,2,2],[0,1,1,2]];
  let p=0;
  for(const [x0,y0,dx,dy] of P){
    const pw=Math.ceil((w-x0)/dx), ph=Math.ceil((h-y0)/dy);
    if(pw<=0||ph<=0) continue;
    const {out,next}=unfilter(raw,p,pw,ph,ch); p=next;
    for(let y=0;y<ph;y++) for(let x=0;x<pw;x++)
      out.copy(data, (y0+y*dy)*stride + (x0+x*dx)*ch, (y*pw+x)*ch, (y*pw+x+1)*ch);
  }
  return {w,h,ch,data};
}
module.exports={loadPNG};
