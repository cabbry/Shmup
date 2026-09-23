// A real render of the ship: per-pixel texture, smoothed normals with a
// crease, key + fill + rim, specular, and supersampling.
const fs=require("fs"), zlib=require("zlib");
const {loadMesh} = require(__dirname+"/mesh.js");
const {loadPNG}  = require(__dirname+"/png.js");

const M = loadMesh(process.env.MESH || "data/data/models/players/hpp_ship.obj.md5mesh");
const T = loadPNG(process.env.TEX  || "data/data/textures/object/hpp1.png");

const sub=(a,b)=>[a[0]-b[0],a[1]-b[1],a[2]-b[2]];
const cross=(a,b)=>[a[1]*b[2]-a[2]*b[1], a[2]*b[0]-a[0]*b[2], a[0]*b[1]-a[1]*b[0]];
const dot=(a,b)=>a[0]*b[0]+a[1]*b[1]+a[2]*b[2];
const norm=a=>{const l=Math.hypot(a[0],a[1],a[2])||1; return [a[0]/l,a[1]/l,a[2]/l];};
const rotY=(p,a)=>{const c=Math.cos(a),s=Math.sin(a);return [c*p[0]+s*p[2],p[1],-s*p[0]+c*p[2]];};
const rotX=(p,a)=>{const c=Math.cos(a),s=Math.sin(a);return [p[0],c*p[1]-s*p[2],s*p[1]+c*p[2]];};
const rotZ=(p,a)=>{const c=Math.cos(a),s=Math.sin(a);return [c*p[0]-s*p[1],s*p[0]+c*p[1],p[2]];};

function bilinear(u,v){
  let x=u*(T.w-1), y=(1-v)*(T.h-1);
  x=Math.max(0,Math.min(T.w-1,x)); y=Math.max(0,Math.min(T.h-1,y));
  const ix=Math.floor(x), iy=Math.floor(y), fx=x-ix, fy=y-iy;
  const g=(xx,yy)=>{ const o=(Math.min(T.h-1,yy)*T.w+Math.min(T.w-1,xx))*T.ch; return [T.data[o],T.data[o+1],T.data[o+2]]; };
  const a=g(ix,iy), b=g(ix+1,iy), c=g(ix,iy+1), d=g(ix+1,iy+1);
  return [0,1,2].map(i => (a[i]*(1-fx)+b[i]*fx)*(1-fy) + (c[i]*(1-fx)+d[i]*fx)*fy);
}

function render(o){
  const SS=o.ss||3, S=o.size*SS;
  const yaw=o.yaw*Math.PI/180, pitch=o.pitch*Math.PI/180, roll=(o.roll||0)*Math.PI/180;
  const P = M.pos.map(p => rotZ(rotX(rotY(p,yaw),pitch),roll));

  // vertex normals, area weighted
  const VN = P.map(()=>[0,0,0]);
  const FN = [];
  M.tris.forEach((t,i)=>{
    const n = cross(sub(P[t[1]],P[t[0]]), sub(P[t[2]],P[t[0]]));
    FN[i]=n;
    for(const k of t){ VN[k][0]+=n[0]; VN[k][1]+=n[1]; VN[k][2]+=n[2]; }
  });
  for(let i=0;i<VN.length;i++) VN[i]=norm(VN[i]);
  const crease = Math.cos((o.crease===undefined?62:o.crease)*Math.PI/180);

  let mn=[1e9,1e9,1e9],mx=[-1e9,-1e9,-1e9];
  for(const p of P) for(let i=0;i<3;i++){ if(p[i]<mn[i])mn[i]=p[i]; if(p[i]>mx[i])mx[i]=p[i]; }
  const pad=o.pad===undefined?0.05:o.pad, z2=o.zoom||1;
  const k=(S*(1-2*pad))/Math.max(mx[0]-mn[0], mx[1]-mn[1]);
  const cx=(mn[0]+mx[0])/2, cy=(mn[1]+mx[1])/2;
  const shx=(o.shift?o.shift[0]:0)*S, shy=(o.shift?o.shift[1]:0)*S;
  const px = p => [S/2 + (p[0]-cx)*k*z2 + shx, S/2 - (p[1]-cy)*k*z2 + shy];

  const L  = norm(o.light||[-0.5,0.72,0.55]);        // key
  const L2 = norm(o.fill ||[0.7,-0.25,0.35]);        // fill, from the other side
  const E  = [0,0,1];
  const H  = norm([L[0]+E[0], L[1]+E[1], L[2]+E[2]]);

  const img=Buffer.alloc(S*S*4);
  const bgc=o.rgb||[0,0,0];
  for(let i=0;i<S*S;i++){ img[i*4]=bgc[0]; img[i*4+1]=bgc[1]; img[i*4+2]=bgc[2]; img[i*4+3]=o.rgb?255:0; }
  const zb=new Float32Array(S*S).fill(-1e9);

  M.tris.forEach((t,ti)=>{
    const fn=norm(FN[ti]);
    if(fn[2]<=0) return;                              // back faces
    const A=P[t[0]],B=P[t[1]],C=P[t[2]];
    const a=px(A), b=px(B), c=px(C);
    const uv=t.map(i=>[M.verts[i].u, M.verts[i].v]);
    const nn=t.map(i => dot(fn,VN[i])>crease ? VN[i] : fn);   // keep the creases hard
    const x0=Math.max(0,Math.floor(Math.min(a[0],b[0],c[0]))), x1=Math.min(S-1,Math.ceil(Math.max(a[0],b[0],c[0])));
    const y0=Math.max(0,Math.floor(Math.min(a[1],b[1],c[1]))), y1=Math.min(S-1,Math.ceil(Math.max(a[1],b[1],c[1])));
    const den=(b[1]-c[1])*(a[0]-c[0])+(c[0]-b[0])*(a[1]-c[1]);
    if(Math.abs(den)<1e-9) return;
    for(let y=y0;y<=y1;y++) for(let x=x0;x<=x1;x++){
      const l1=((b[1]-c[1])*(x+0.5-c[0])+(c[0]-b[0])*(y+0.5-c[1]))/den;
      const l2=((c[1]-a[1])*(x+0.5-c[0])+(a[0]-c[0])*(y+0.5-c[1]))/den;
      const l3=1-l1-l2;
      if(l1<0||l2<0||l3<0) continue;
      const z=l1*A[2]+l2*B[2]+l3*C[2];
      const idx=y*S+x;
      if(z<=zb[idx]) continue;
      zb[idx]=z;
      const u=l1*uv[0][0]+l2*uv[1][0]+l3*uv[2][0];
      const v=l1*uv[0][1]+l2*uv[1][1]+l3*uv[2][1];
      const n=norm([l1*nn[0][0]+l2*nn[1][0]+l3*nn[2][0],
                    l1*nn[0][1]+l2*nn[1][1]+l3*nn[2][1],
                    l1*nn[0][2]+l2*nn[1][2]+l3*nn[2][2]]);
      const alb=bilinear(u,v);
      const key=Math.max(0,dot(n,L)), fill=Math.max(0,dot(n,L2));
      const rim=Math.pow(1-Math.max(0,n[2]), 3);
      const spec=Math.pow(Math.max(0,dot(n,H)), o.shine===undefined?42:o.shine);
      const amb=o.amb===undefined?0.34:o.amb;
      const g = amb + (o.key===undefined?1.05:o.key)*key + (o.fillK===undefined?0.28:o.fillK)*fill;
      const w = (o.rimK===undefined?0.30:o.rimK)*rim*255 + (o.specK===undefined?0.55:o.specK)*spec*255;
      const oo=idx*4;
      for(let i=0;i<3;i++) img[oo+i]=Math.min(255, Math.round(alb[i]*g + w));
      img[oo+3]=255;
    }
  });

  // downsample
  const D=o.size, out=Buffer.alloc(D*D*4);
  for(let y=0;y<D;y++) for(let x=0;x<D;x++){
    let r=0,g=0,b=0,al=0;
    for(let j=0;j<SS;j++) for(let i=0;i<SS;i++){
      const o2=((y*SS+j)*S + x*SS+i)*4;
      r+=img[o2]; g+=img[o2+1]; b+=img[o2+2]; al+=img[o2+3];
    }
    const n=SS*SS, d=(y*D+x)*4;
    out[d]=Math.round(r/n); out[d+1]=Math.round(g/n); out[d+2]=Math.round(b/n); out[d+3]=Math.round(al/n);
  }
  return {img:out, size:D};
}

function crc32(b){ let TB=crc32.t; if(!TB){TB=crc32.t=[];for(let n=0;n<256;n++){let c=n;for(let k=0;k<8;k++)c=c&1?0xEDB88320^(c>>>1):c>>>1;TB[n]=c>>>0;}}
  let c=0xFFFFFFFF; for(const x of b) c=TB[(c^x)&255]^(c>>>8); return (c^0xFFFFFFFF)>>>0; }
function writePNG(path,S,img){
  const ck=(ty,d)=>{const l=Buffer.alloc(4);l.writeUInt32BE(d.length);const td=Buffer.concat([Buffer.from(ty,"latin1"),d]);const c=Buffer.alloc(4);c.writeUInt32BE(crc32(td)>>>0);return Buffer.concat([l,td,c]);};
  const stride=S*4, raw=Buffer.alloc((stride+1)*S);
  for(let y=0;y<S;y++){raw[y*(stride+1)]=0;img.copy(raw,y*(stride+1)+1,y*stride,(y+1)*stride);}
  const ih=Buffer.alloc(13);ih.writeUInt32BE(S,0);ih.writeUInt32BE(S,4);ih[8]=8;ih[9]=6;
  fs.writeFileSync(path,Buffer.concat([Buffer.from([137,80,78,71,13,10,26,10]),ck("IHDR",ih),ck("IDAT",zlib.deflateSync(raw)),ck("IEND",Buffer.alloc(0))]));
}
module.exports={render, writePNG};
