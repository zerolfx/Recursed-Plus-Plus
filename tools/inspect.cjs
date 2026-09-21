// Read-only investigation of the exact installed x86 build.
const fs = require('node:fs');
const path = process.argv[2] || 'C:/Program Files (x86)/Steam/steamapps/common/Recursed/Recursed.exe';
const b = fs.readFileSync(path);
const pe = b.readUInt32LE(60), opt = pe + 24;
const base = b.readUInt32LE(opt + 28);
const sections = [];
for (let i = 0, p = opt + b.readUInt16LE(pe + 20); i < b.readUInt16LE(pe + 6); i++, p += 40)
  sections.push({name:b.toString('ascii',p,p+8).replace(/\0/g,''),rva:b.readUInt32LE(p+12),raw:b.readUInt32LE(p+20),size:b.readUInt32LE(p+16)});
const va = off => { const s=sections.find(s=>off>=s.raw&&off<s.raw+s.size); return s ? base+s.rva+off-s.raw : 0; };
const off = addr => { const s=sections.find(s=>addr>=base+s.rva&&addr<base+s.rva+s.size); return s ? s.raw+addr-base-s.rva : -1; };
const hex = n => '0x'+n.toString(16).padStart(8,'0');
function refs(addr) { const word=Buffer.alloc(4); word.writeUInt32LE(addr);const out=[];for(let i=0;(i=b.indexOf(word,i))>=0;i++)out.push(i);return out; }
for(const name of ['Chest','Item','Player','Game','Jar','Cauldron','Entity']){
  const at=b.indexOf(Buffer.from('.?AV'+name+'@@\0'));if(at<0)continue;
  const td=va(at)-8, tables=[];
  for(const r of refs(td)){
    const col=r-12;if(col<0||b.readUInt32LE(col)!==0||b.readUInt32LE(col+4)>256)continue;
    for(const v of refs(va(col))){
      const entries=[];for(let p=v+4;p+4<b.length&&entries.length<40;p+=4){const a=b.readUInt32LE(p);const s=sections.find(s=>a>=base+s.rva&&a<base+s.rva+s.size);if(s?.name!=='.text')break;entries.push(hex(a));}
      if(entries.length)tables.push({vtable:hex(va(v+4)),entries});
    }
  }
  console.log(JSON.stringify({name,descriptor:hex(td),tables}));
}
if(process.argv[3]){
  const a=Number(process.argv[3]), p=off(a);console.log(hex(a),b.subarray(p,p+64).toString('hex'));
}
