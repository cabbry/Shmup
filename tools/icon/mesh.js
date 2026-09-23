const fs=require("fs"), zlib=require("zlib");

// ---- MD5 mesh (one joint at identity: a vertex is its single weight)
function loadMesh(path){
  const t=fs.readFileSync(path,"utf8");
  const verts=[], tris=[], weights=[];
  for (const m of t.matchAll(/^\s*vert\s+(\d+)\s+\(\s*([-\d.eE]+)\s+([-\d.eE]+)\s*\)\s+(\d+)\s+(\d+)/gm))
    verts[+m[1]]={u:+m[2], v:+m[3], w0:+m[4], wn:+m[5]};
  for (const m of t.matchAll(/^\s*tri\s+(\d+)\s+(\d+)\s+(\d+)\s+(\d+)/gm))
    tris[+m[1]]=[+m[2],+m[3],+m[4]];
  for (const m of t.matchAll(/^\s*weight\s+(\d+)\s+(\d+)\s+([-\d.eE]+)\s+\(\s*([-\d.eE]+)\s+([-\d.eE]+)\s+([-\d.eE]+)\s*\)/gm))
    weights[+m[1]]={bias:+m[3], p:[+m[4],+m[5],+m[6]]};
  const pos = verts.map(v => {
    let p=[0,0,0];
    for (let k=0;k<v.wn;k++){ const w=weights[v.w0+k]; p[0]+=w.p[0]*w.bias; p[1]+=w.p[1]*w.bias; p[2]+=w.p[2]*w.bias; }
    return p;
  });
  return {verts, tris, pos};
}

// ---- PNG, 8-bit, colour type 2 or 6, not interlaced
function loadPNG(path){
  const b=fs.readFileSync(path);
  const w=b.readUInt32BE(16), h=b.readUInt32BE(20), ct=b[25];
  const ch = ct===6?4:ct===2?3:0;
  if(!ch) throw new Error("colour type "+ct+" not handled");
  let idat=[], o=8;
  while(o<b.length){
    const len=b.readUInt32BE(o), type=b.toString("latin1",o+4,o+8);
    if(type==="IDAT") idat.push(b.slice(o+8,o+8+len));
    o += 12+len;
    if(type==="IEND") break;
  }
  const raw=zlib.inflateSync(Buffer.concat(idat));
  const out=Buffer.alloc(w*h*ch), stride=w*ch;
  let p=0;
  for(let y=0;y<h;y++){
    const f=raw[p++]; const line=raw.slice(p,p+stride); p+=stride;
    for(let x=0;x<stride;x++){
      const a = x>=ch ? out[y*stride+x-ch] : 0;
      const bb = y>0 ? out[(y-1)*stride+x] : 0;
      const c = (x>=ch && y>0) ? out[(y-1)*stride+x-ch] : 0;
      let v=line[x];
      if(f===1) v+=a; else if(f===2) v+=bb; else if(f===3) v+=(a+bb)>>1;
      else if(f===4){ const pa=Math.abs(bb-c), pb=Math.abs(a-c), pc=Math.abs(a+bb-2*c);
                      v += (pa<=pb&&pa<=pc)?a:(pb<=pc?bb:c); }
      out[y*stride+x]=v&255;
    }
  }
  return {w,h,ch,data:out};
}
module.exports={loadMesh, loadPNG: require(__dirname+"/png.js").loadPNG};
