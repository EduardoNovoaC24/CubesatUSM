'use strict';
(function(){
  const intro=document.getElementById('intro');
  const dismiss=()=>{intro?.classList.add('dismissed');setTimeout(()=>intro?.remove(),700);};
  document.getElementById('enter')?.addEventListener('click',dismiss);
  setTimeout(dismiss,3100);
  const tabs=[...document.querySelectorAll('.tab-btn')];
  tabs.forEach(btn=>btn.addEventListener('click',()=>{
    tabs.forEach(b=>b.setAttribute('aria-selected',String(b===btn)));
    document.querySelectorAll('.tab-panel').forEach(p=>p.classList.toggle('active',p.id===btn.dataset.target));
    if(btn.dataset.target==='tab-radar') setTimeout(()=>window.__radarMap?.invalidateSize(),90);
  }));

  const metricNames={ax:'AX',ay:'AY',az:'AZ',gx:'GX',gy:'GY',gz:'GZ',mx:'MX',my:'MY',mz:'MZ'};
  document.querySelectorAll('#imu-metrics-1,#imu-metrics-2').forEach(el=>{
    for(const [key,label] of Object.entries(metricNames)){
      const div=document.createElement('div');
      const sm=document.createElement('small');sm.textContent=label;
      const b=document.createElement('b');b.dataset.imu=key;b.textContent='—';
      div.append(sm,b);el.appendChild(div);
    }
  });
  let picoState={capable:false,connected:false};
  let benchStartId, resultsId;
  let benchWatch=false;
  let resultsAt=0;
  const lanKey=document.getElementById('lan-key');
  const lanOperator=document.getElementById('lan-operator');
  const lanStatus=document.getElementById('lan-status');
  const lanFeedback=document.getElementById('lan-feedback');
  const lanClient=lanKey?Array.from(crypto.getRandomValues(new Uint8Array(16)),byte=>byte.toString(16).padStart(2,'0')).join(''):'';
  function commandRequest(payload){
    const headers={'Content-Type':'application/json'};
    if(lanKey){
      headers['X-Station-Key']=lanKey.value.trim();
      headers['X-Station-Client']=lanClient;
      payload.operator=lanOperator.value.trim();
    }
    return {method:'POST',headers,body:JSON.stringify(payload)};
  }
  document.getElementById('lan-release')?.addEventListener('click',async()=>{
    try{
      const response=await fetch('/api/control/release',commandRequest({}));
      if(!response.ok)throw Error((await response.json()).error);
      await pollPico();
      lanFeedback.textContent='';
    }catch(error){lanFeedback.textContent=error.message;}
  });
  let wheelTelemetryAt=0;
  const wheelSetpoint=document.querySelector('[data-cmd="WHEEL"][data-arg]');
  const wheelCountdown=document.getElementById('wheel-ready-countdown');
  const wheelReadyWindow=20;
  function updateWheelSetpoint(){
    const wheel=picoState.wheel;
    const age=wheel?wheel.age+(performance.now()-wheelTelemetryAt)/1000:Infinity;
    const fresh=picoState.connected&&picoState.capable&&age<=25;
    const ready=fresh&&wheel.state===2;
    const seconds=ready?Math.max(0,Math.ceil(wheelReadyWindow-(wheel.elapsed_s??0)-age)):0;
    const manualRunning=fresh&&wheel.state===3&&![1,2].includes(wheel.detumble);
    const benchArmed=picoState.brake_batch?.armed;
    wheelSetpoint.disabled=benchArmed||!(seconds>0||manualRunning);
    wheelCountdown.hidden=!ready||benchArmed;
    wheelCountdown.textContent=seconds>0
      ? 'Lista. Tiempo estimado para aplicar el nivel '+seconds+' s.'
      : 'Plazo vencido. Espera a que la rueda vuelva a Desarmada.';
  }
  const fmt=(v,unit='',places=0)=>typeof v==='number'&&Number.isFinite(v)?v.toFixed(places)+unit:'—';
  const modelAxis=document.getElementById('model-long-axis');
  try{
    const savedAxis=localStorage.getItem('cubesat-model-axis');
    if(['X','Y','Z'].includes(savedAxis))modelAxis.value=savedAxis;
  }catch{ /* Optional display preference. */ }
  modelAxis.addEventListener('change',()=>{
    try{localStorage.setItem('cubesat-model-axis',modelAxis.value);}catch{}
    modelQuaternion(picoState.latest);
  });
  // Fusion rotates body coordinates into NWU (+Z up). CSS places up at −Y.
  // The mesh's long dimension is local Y, independent of how the GY‑91 is mounted.
  function modelQuaternion(r){
    if(!r||![r.q0,r.q1,r.q2,r.q3].every(v=>typeof v==='number')) return;
    let [w,x,y,z]=[r.q0,r.q1,r.q2,r.q3];
    const norm=Math.hypot(w,x,y,z);if(norm<.001)return;
    [w,x,y,z]=[w/norm,x/norm,y/norm,z/norm];
    const columns=[
      [1-2*(y*y+z*z),2*(x*y+z*w),2*(x*z-y*w)],
      [2*(x*y-z*w),1-2*(x*x+z*z),2*(y*z+x*w)],
      [2*(x*z+y*w),2*(y*z-x*w),1-2*(x*x+y*y)],
    ];
    const meshAxes={ X:[[1,1],[0,-1],[2,1]],Y:[[0,1],[1,-1],[2,-1]],Z:[[0,1],[2,-1],[1,1]] };
    const m=meshAxes[modelAxis.value].flatMap(([axis,sign])=>{
      const [north,west,up]=columns[axis];
      return [sign*north,-sign*up,sign*west,0];
    });
    m.push(0,0,0,1);
    const matrix='matrix3d('+m.join(',')+')';
    ['sat-model-1','sat-model-2'].forEach(id=>document.getElementById(id).style.transform=matrix);
  }
  const azimuthScene=document.getElementById('azimuth-scene');
  const azimuthCamera=document.getElementById('azimuth-camera');
  const azimuthModel=document.getElementById('azimuth-model');
  const orbit=document.getElementById('azimuth-orbit');
  const elevation=document.getElementById('azimuth-elevation');
  const judgeAzimuth=document.getElementById('true-azimuth');
  const measureLight=document.getElementById('measure-light');
  let lightMeasurement=null;
  let cameraDrag;
  const wrapDegrees=angle=>((angle%360)+360)%360;
  const showCamera=()=>azimuthCamera.style.transform=`rotateX(${elevation.value}deg) rotateY(${orbit.value}deg)`;
  [orbit,elevation].forEach(input=>input.addEventListener('input',showCamera));
  showCamera();
  azimuthScene.addEventListener('pointerdown',event=>{
    if(event.button!==0)return;
    azimuthScene.setPointerCapture(event.pointerId);
    cameraDrag={x:event.clientX,y:event.clientY,az:Number(orbit.value),el:Number(elevation.value)};
  });
  azimuthScene.addEventListener('pointermove',event=>{
    if(!cameraDrag||!azimuthScene.hasPointerCapture(event.pointerId))return;
    orbit.value=wrapDegrees(cameraDrag.az+(event.clientX-cameraDrag.x)*.6+180)-180;
    elevation.value=Math.max(-75,Math.min(75,cameraDrag.el-(event.clientY-cameraDrag.y)*.6));
    showCamera();
  });
  azimuthScene.addEventListener('lostpointercapture',()=>cameraDrag=undefined);

  function stationaryLight(wheel){
    if(!wheel||wheel.age>25||![0,2].includes(wheel.state)||!Number.isFinite(wheel.body_rpm)||Math.abs(wheel.body_rpm)>.08)return null;
    const channels=[wheel.light_1,wheel.light_2,wheel.light_3,wheel.light_4];
    if(!channels.every(Number.isFinite))return null;
    const sign=document.getElementById('light-invert').checked?-1:1;
    const x=sign*(channels[0]-channels[2]),y=sign*(channels[1]-channels[3]);
    return Math.hypot(x,y)>=80?Math.atan2(y,x)*180/Math.PI:null;
  }
  function renderAzimuth(wheel){
    const tracking=wheel?.maneuver===3;
    const rotation=tracking?wheel.rotation_deg:null;
    const reference=lightMeasurement?.reference??(tracking?wheel.reference_deg:null);
    const light=lightMeasurement?.light??(tracking?wheel.light_azimuth_deg:null);
    document.getElementById('maneuver-reference').textContent=fmt(reference,'°',1);
    document.getElementById('maneuver-light').textContent=fmt(light,'°',1);
    document.getElementById('azimuth-rotation').textContent=fmt(rotation,'°',1);
    if(Number.isFinite(rotation))azimuthModel.style.transform=`rotateY(${-rotation}deg)`;
    else azimuthModel.style.transform='';
    const target=judgeAzimuth.valueAsNumber;
    const error=Number.isFinite(light)&&Number.isFinite(target)&&judgeAzimuth.validity.valid?
      Math.abs(wrapDegrees(light-target+180)-180):null;
    document.getElementById('azimuth-error').textContent=error===null?'—':
      error.toFixed(1)+'° · '+(error<=5?'≤5°':error<=15?'≤15°':error<=30?'≤30°':'>30°');
    measureLight.disabled=stationaryLight(wheel)===null;
  }
  measureLight.addEventListener('click',()=>{
    const angle=stationaryLight(picoState.wheel);
    const reference=document.getElementById('reference-deg');
    if(angle===null||!reference.reportValidity())return;
    lightMeasurement={reference:reference.valueAsNumber,light:wrapDegrees(reference.valueAsNumber+angle)};
    renderAzimuth(picoState.wheel);
  });
  judgeAzimuth.addEventListener('input',()=>renderAzimuth(picoState.wheel?.age<=25?picoState.wheel:null));
  function drawChart(id,history,keys,colors){
    const svg=document.getElementById(id);if(!svg)return;
    const ns='http://www.w3.org/2000/svg';svg.replaceChildren();svg.setAttribute('viewBox','0 0 540 218');
    const add=(name,attrs,text)=>{const el=document.createElementNS(ns,name);Object.entries(attrs).forEach(([k,v])=>el.setAttribute(k,v));if(text!==undefined)el.textContent=text;svg.append(el);return el;};
    const data=history||[];let nums=[];
    for(const p of data){for(const k of keys)if(typeof p[k]==='number'&&Number.isFinite(p[k]))nums.push(p[k]);}
    if(nums.length<2){add('text',{x:270,y:112,'text-anchor':'middle',fill:'#b57b84','font-size':14},'Esperando más muestras');return;}
    let lo=Math.min(...nums),hi=Math.max(...nums);if(Math.abs(hi-lo)<.001){hi+=.5;lo-=.5;}else{const pad=(hi-lo)*.12;lo-=pad;hi+=pad;}
    for(let i=0;i<=4;i++){
      const y=19+i*43;
      add('line',{x1:47,y1:y,x2:526,y2:y,stroke:'#4c242a','stroke-width':1,'stroke-dasharray':'3 7'});
      add('text',{x:42,y:y+4,'text-anchor':'end',fill:'#c79199','font-size':11},(hi-(hi-lo)*i/4).toFixed(1));
    }
    const x0=data[0]?.t||0,x1=data.at(-1)?.t||x0+1,dt=Math.max(1,x1-x0);
    for(let k=0;k<keys.length;k++){
      let run=[];
      const flush=()=>{if(run.length>1)add('polyline',{points:run.join(' '),fill:'none',stroke:colors[k], 'stroke-width':2.3,'stroke-linejoin':'round','stroke-linecap':'round'});run=[];};
      for(const row of data){
        const val=row[keys[k]];
        if(typeof val!=='number'||!Number.isFinite(val)){flush();continue;}
        const x=47+479*((row.t-x0)/dt),y=19+172*(hi-val)/(hi-lo);
        run.push(x.toFixed(1)+','+y.toFixed(1));
      }
      flush();
    }
    add('text',{x:526,y:210,'text-anchor':'end',fill:'#b97a83','font-size':11},'Últimos '+Math.round(dt)+' s');
  }
  function benchStartBlock(wheel, wheelFresh, adcsFresh, batch) {
    if(!wheelFresh)return 'Esperando telemetría reciente de la rueda por LoRa.';
    if(!adcsFresh)return 'Llegan datos de la rueda, pero falta su estado ADCS.';
    if(wheel.axis>=3)return 'Pulsa Aplicar eje con el eje y la polaridad del montaje.';
    if(!wheel.gyro_calibrated)return 'Giroscopio sin calibrar. Deja quieto el CubeSat para que el STM32 calcule el bias.';
    if(wheel.state===4)return 'La rueda está deteniéndose. Espera a que indique Desarmada.';
    if(wheel.state===1)return 'El ESC se está armando. Espera a que indique Lista.';
    if(wheel.state===3)return 'Hay una prueba en curso. Para cambiar de prueba, pulsa Detener y espera a que indique Desarmada.';
    if(wheel.state===2&&!batch?.armed)return 'La rueda está armada en modo manual. Pulsa Detener y espera a que indique Desarmada.';
    if(!Number.isFinite(wheel.body_rpm))return 'Esperando una lectura válida del giroscopio en el eje aplicado.';
    if(Math.abs(wheel.body_rpm)>0.08)return 'Espera a que el CubeSat quede quieto antes de iniciar.';
    return '';
  }
  function renderBrakeBatch(batch, wheelReady, benchBlock) {
    const seconds=batch?.seconds||[];
    const states=['Sin tanda','Acelerando','Frenando con el giroscopio','Preparando la siguiente repetición','Tanda completa','Tanda interrumpida'];
    const errors=['','Cancelada','Giro no válido','Se perdió el enlace','El PWM no produjo un giro medible','El frenado no se estabilizó','El montaje no volvió al reposo inicial'];
    if(batch)document.getElementById('adcs-result').textContent=
      states[batch.phase]+(batch.pulse?' · '+batch.pulse+' µs':'')+(batch.error?' · '+errors[batch.error]:'');
    document.getElementById('brake-count').textContent=seconds.length+' / 20';
    document.getElementById('brake-best').textContent=seconds.length?Math.min(...seconds).toFixed(2)+' s':'—';
    const running=batch?.phase>=1&&batch?.phase<=3;
    const canRun=wheelReady&&batch?.armed&&!running&&!benchBlock;
    document.querySelector('[data-prefix="RUNS"]').disabled=!canRun;
    let generateHint='Pulsa Iniciar y espera la confirmación del STM32.';
    if(running)generateHint='La tanda ya está en curso.';
    else if(canRun)generateHint='Armado bidireccional confirmado. Puedes generar las 20 pruebas.';
    else if(batch?.armed&&benchBlock)generateHint=benchBlock;
    document.getElementById('brake-generate-status').textContent=generateHint;
    const svg=document.getElementById('plot-brake');
    svg.replaceChildren();
    svg.setAttribute('viewBox','0 0 580 216');
    const add=(name,attrs,text)=>{
      const item=document.createElementNS('http://www.w3.org/2000/svg',name);
      for(const [key,value] of Object.entries(attrs))item.setAttribute(key,value);
      if(text!==undefined)item.textContent=text;
      svg.append(item);
    };
    if(!seconds.length){
      add('text',{x:290,y:112,'text-anchor':'middle',fill:'currentColor','font-size':14},'Esperando mediciones');
      return;
    }
    const max=Math.max(1,...seconds)*1.08;
    for(let i=0;i<=4;i++){
      const y=180-i*39;
      add('line',{x1:41,y1:y,x2:572,y2:y,stroke:'var(--line)','stroke-width':1});
      add('text',{x:37,y:y+4,'text-anchor':'end',fill:'currentColor','font-size':11},(max*i/4).toFixed(1));
    }
    const best=Math.min(...seconds);
    for(let i=0;i<20;i++){
      const x=44+i*26.3;
      if(i<seconds.length){
        const height=Math.max(2,156*seconds[i]/max);
        add('rect',{x,y:180-height,width:20,height,rx:3,fill:seconds[i]===best?'var(--lime)':'var(--aqua)'});
      }
      if(i===0||i%5===4)add('text',{x:x+10,y:198,'text-anchor':'middle',fill:'currentColor','font-size':11},i+1);
    }
    add('text',{x:41,y:14,fill:'currentColor','font-size':11},'Tiempo (s)');
  }
  async function pollBrakeBatch(){
    if(!benchWatch||!picoState.connected||!picoState.capable||
      (lanKey&&!lanKey.value.trim())||performance.now()-resultsAt<7500)return;
    resultsAt=performance.now();
    try{
      const response=await (window.stationRequest||fetch)('/api/pico/command',
        commandRequest({command:'WHEEL',args:['RESULTS']}));
      if(response.ok)resultsId=(await response.json()).id;
    }catch{ /* The firmware stops if it loses RF traffic. */ }
  }
  const chartDefs=[['plot-q',['q0','q1','q2','q3'],['#f47588','#4de7ae','#ffd37d','#c7a3ff']],
                   ['plot-norm',['norm'],['#4fe5c0']],
                   ['plot-acc',['ax','ay','az'],['#f47588','#4de7ae','#ffd37d']],
                   ['plot-gyro',['gx','gy','gz'],['#f47588','#4de7ae','#ffd37d']],
                   ['plot-mag',['mx','my','mz'],['#f47588','#4de7ae','#ffd37d']]];
  function renderMissionScene(wheel, adcsFresh){
    const inverted=document.getElementById('light-invert').checked;
    document.querySelectorAll('[data-mission-light]').forEach(sensor=>{
      const face=sensor.dataset.missionLight;
      const value=adcsFresh?wheel['light_'+face]:null;
      const valid=Number.isFinite(value);
      const signal=valid?(inverted?4095-value:value):0;
      sensor.dataset.state=valid?'measured':'unknown';
      sensor.style.setProperty('--signal',String(signal/4095));
      document.querySelector('[data-light-value="'+face+'"]').textContent=valid?value+' ADC':'—';
      const meter=document.querySelector('[data-light-meter="'+face+'"]');
      meter.hidden=!valid;
      meter.value=signal;
    });
    document.getElementById('mission-rotor').dataset.measured=
      String(adcsFresh&&Number.isFinite(wheel.rpm)&&wheel.rpm>0);
  }
  const panelVoltage=document.getElementById('test-panel-voltage');
  const panelStatus=document.getElementById('test-panel-status');
  const reducedMotion=window.matchMedia('(prefers-reduced-motion: reduce)');
  let panelValue=0, panelTarget=0, panelFrame=0;
  function renderTestPanel(data){
    const sample=data.test_panel;
    const fresh=data.connected&&sample?.age<=30&&Number.isFinite(sample.voltage);
    const status=fresh?(sample.saturated?'ADC saturado':'Lectura del STM32'):'Sin lectura reciente';
    if(panelStatus.textContent!==status) panelStatus.textContent=status;
    const target=fresh&&!sample.saturated?sample.voltage:0;
    if(target===panelTarget) return;
    cancelAnimationFrame(panelFrame);
    panelTarget=target;
    const startValue=panelValue;
    const startTime=performance.now();
    function update(now){
      const progress=reducedMotion.matches||!fresh||sample.saturated
        ? 1 : Math.min(1,Math.max(0,(now-startTime)/650));
      panelValue=startValue+(target-startValue)*progress;
      panelVoltage.textContent=panelValue.toFixed(2).padStart(5,'0');
      if(progress<1) panelFrame=requestAnimationFrame(update);
    }
    update(startTime);
  }
  function renderSolar(data){
    renderTestPanel(data);
    const reading=data.solar;
    const fresh=data.connected&&reading&&reading.age<=30;
    const ina=reading?.source==='ina219';
    document.querySelectorAll('[data-eps-analog-only]').forEach(el=>el.hidden=ina);
    document.getElementById('eps-solar-label').textContent=ina?'Línea solar · INA219':'Pareja 1 · PC4';
    document.getElementById('eps-solar-series').textContent=ina?'Línea solar':'Pareja 1';
    document.getElementById('eps-battery-label').textContent=ina?'Voltaje de batería':'Batería · PA1';
    document.getElementById('eps-current-card').hidden=!ina;
    document.getElementById('eps-current').textContent=fresh&&ina?fmt(reading.current_ma,' mA',2):'—';
    const channels=fresh?(reading.channels||[]):[];
    document.querySelectorAll('[data-eps-voltage]').forEach(el=>{
      const channel=channels[Number(el.dataset.epsVoltage)];
      el.textContent=channel?.voltage!=null?
        (channel.saturated?'≥ ':'')+fmt(channel.voltage,' V',2):'—';
    });
    const battery=channels[3];
    const fill=document.getElementById('eps-battery-fill');
    fill.style.width=battery?.voltage!=null&&!battery.saturated?
      Math.max(0,Math.min(100,battery.voltage/16*100))+'%':'0%';
    const legacy=document.getElementById('solar-legacy');
    legacy.hidden=!(fresh&&!reading.channels&&reading.voltage!=null);
    legacy.textContent=legacy.hidden?'':'Medición del firmware de un canal: '+fmt(reading.voltage,' V',2);
    document.getElementById('solar-age').textContent=reading?fmt(reading.age,' s',0):'—';
    document.getElementById('solar-status').textContent=!fresh?'Sin telemetría EPS reciente':
      channels.some(channel=>channel.saturated)||reading.saturated?'Entrada ADC saturada. Revisa el divisor.':
      battery?.voltage>16?'Batería por encima de 16 V':
      channels.some(channel=>channel.voltage!=null)?(ina?'Datos recibidos desde la ESP32':'Voltajes estimados a partir del divisor'):
      legacy.hidden?'Lecturas ADC no disponibles':'Esperando la trama EPS de cuatro canales';
    drawChart('plot-solar',data.solar_history||[],['pair1','pair2','pair3','battery'],
      ['#f6b55b','#ee8394','#81caff','#8fe4aa']);
  }
  function renderPico(data){
    picoState=data;
    if(lanStatus && data.shared_control){
      const control=data.shared_control;
      lanStatus.textContent=control.operator?
        'Turno: '+control.operator+' · '+control.seconds+' s':
        'Control disponible · telemetría compartida';
    }
    renderSolar(data);
    const label=data.connected?(data.capable?'Pico conectado · comandos disponibles':'Pico conectado · esperando PICO_READY'):
      (data.error?'Pico desconectado':'Pico no conectado');
    for(const id of ['pico-status-1','pico-status-2','pico-status-3']){
      const el=document.getElementById(id);el.textContent=label;el.style.color=data.capable?'#57e8b6':'#e1bac0';
    }
    const mp={packets:data.packets,samples:data.samples,
              rssi:fmt(data.rf?.rssi,' dBm',1),snr:fmt(data.rf?.snr,' dB',1),freq:fmt(data.rf?.freq_error,' Hz',1)};
    document.querySelectorAll('[data-pico]').forEach(el=>el.textContent=mp[el.dataset.pico]??'—');
    const latest=data.latest;
    let magStatus='Estado sin datos';
    const wheelMag=data.wheel?.age<=25&&typeof data.wheel.mag_valid==='boolean'?data.wheel:null;
    const magValid=wheelMag?.mag_valid??latest?.mag_valid;
    const magCalibrated=wheelMag?.mag_calibrated??latest?.mag_calibrated;
    if(magValid)magStatus=magCalibrated?'Con datos · calibrado':'Con datos · sin calibrar';
    else if(latest?.mag_detected===false)magStatus='No detectado';
    else if(latest?.mag_detected===true)magStatus='Detectado · sin muestra válida';
    else if(wheelMag)magStatus='Sin muestra válida · detección pendiente';
    document.querySelectorAll('[data-mag-status]').forEach(el=>el.textContent=magStatus);
    document.querySelectorAll('[data-sensor-age]').forEach(el=>{
      el.textContent=latest?.sensor_age!=null?'Última lectura de sensores hace '+Math.round(latest.sensor_age)+' s':'Esperando telemetría de sensores';
    });
    document.getElementById('baro-pressure').textContent=fmt(data.barometer?.pressure_pa,' Pa',0);
    document.getElementById('baro-temperature').textContent=fmt(data.barometer?.temperature_c,' \u00b0C',2);
    document.querySelectorAll('[data-imu]').forEach(el=>{
      const key=el.dataset.imu;
      el.textContent=latest?fmt(latest[key],key==='heading'?'°':'',key==='norm'?3:key==='heading'?1:0):'—';
    });
    document.getElementById('quat-text').textContent=latest?
      ['q0','q1','q2','q3'].map(k=>fmt(latest[k],'',3)).join(' / '):'—';
    if(latest){
      modelQuaternion(latest);
      const compass=document.getElementById('compass-1');
      if(latest.heading!==null && Number.isFinite(latest.heading))
        compass.style.transform='rotate('+latest.heading+'deg)';
    }
    document.getElementById('pico-payload').textContent=data.payload||'Aún no hay paquetes recibidos.';
    const enabled=data.connected&&data.capable;
    document.querySelectorAll('[data-cmd]').forEach(el=>el.disabled=!enabled);
    const wheel=data.wheel;
    const wheelFresh=enabled&&wheel&&wheel.age<=25;
    const wheelStates=['Desarmada','Armando','Lista','En prueba','Deteniendo'];
    const wheelProfiles=['perfil sin seleccionar','avión','bidireccional'];
    document.getElementById('wheel-state').textContent=wheelFresh?
      wheelStates[wheel.state]+' · '+wheelProfiles[wheel.profile]+' · hace '+Math.round(wheel.age)+' s':
      'Sin telemetría reciente de la rueda';
    document.getElementById('wheel-rpm').textContent=wheelFresh&&wheel.rpm!==null?wheel.rpm:'—';
    document.getElementById('wheel-pulse').textContent=wheelFresh?(wheel.pulse?wheel.pulse+' µs':'Sin pulsos'):'—';
    document.getElementById('wheel-level').textContent=wheelFresh?wheel.level:'—';
    const wheelIdle=wheelFresh&&wheel.state===0;
    document.querySelectorAll('[data-cmd="WHEEL"][data-value^="PROFILE_"]').forEach(btn=>{
      btn.disabled=!wheelIdle;
    });
    document.querySelector('[data-cmd="WHEEL"][data-value="ENABLE"]').disabled=!(wheelIdle&&wheel.profile);
    document.querySelector('[data-cmd="WHEEL"][data-value="ARM"]').disabled=!(wheelIdle&&wheel.profile);
    wheelTelemetryAt=performance.now();
    updateWheelSetpoint();
    const adcsFresh=wheelFresh&&wheel.detumble!=null;
    renderMissionScene(wheel,adcsFresh);
    const results=['Sin prueba','Maniobra en curso','Estabilizado','Tiempo agotado',
      'Giro no válido','Detenida por operador','Límite de mando','Giro en aumento','Enlace perdido','Luz sin dirección detectable'];
    document.getElementById('adcs-axis').textContent=adcsFresh&&wheel.axis<3?
      'XYZ'[wheel.axis]+(wheel.inverted?'−':'+'):'—';
    document.getElementById('adcs-rate').textContent=adcsFresh?fmt(wheel.body_rpm,'',2):'—';
    const activeTest=adcsFresh?(wheel.maneuver??(wheel.detumble?1:0)):0;
    document.getElementById('adcs-elapsed').textContent=activeTest===1?fmt(wheel.elapsed_s,' s',2):'—';
    document.getElementById('brake-start-rpm').textContent=activeTest===1?fmt(wheel.brake_initial_rpm,' rpm',1):'—';
    document.getElementById('brake-competition-time').textContent=activeTest===1?
      (Number.isFinite(wheel.brake_initial_rpm)&&wheel.brake_initial_rpm<5?'Inicio bajo 5 rpm':
        fmt(wheel.brake_to_5rpm_s,' s',1)):'—';
    document.querySelectorAll('[data-test-result]').forEach(el=>{
      const status=activeTest===Number(el.dataset.testResult)?results[wheel.detumble]:
        adcsFresh?'Sin prueba':'Sin telemetría ADCS reciente';
      if(el.textContent!==status)el.textContent=status;
    });
    document.getElementById('adcs-sensors').textContent=adcsFresh?
      (wheel.gyro_calibrated?'Giro calibrado':'Giro sin calibrar')+' · '+
      (wheel.mag_valid?(wheel.mag_calibrated?'Mag calibrado':'Mag sin calibrar'):'Mag sin dato válido')+
      ' · '+(wheel.fusion_9dof?'Fusión 9DOF':'Fusión 6DOF'):'Esperando datos del GY‑91';
    document.querySelector('[data-prefix="AXIS_"]').disabled=!wheelIdle;
    const maneuverReady=adcsFresh&&wheel.profile===2&&wheel.state===2&&wheel.axis<3&&
      Number.isFinite(wheel.body_rpm);
    const bench= data.brake_batch;
    const benchCommand=data.commands?.find(cmd=>cmd.id===benchStartId);
    if(benchCommand?.state==='OK'){
      benchStartId=undefined;
      benchWatch=true;
      resultsAt=-Infinity;
    }else if(benchCommand && ['ERR','PICO ERR','SIN CONFIRMACIÓN'].includes(benchCommand.state)){
      benchStartId=undefined;
    }
    const report=data.commands?.find(cmd=>cmd.id===resultsId);
    if(report?.state==='OK'){
      resultsId=undefined;
      if(!bench?.armed)benchWatch=false;
    }
    const startBlock=benchStartBlock(wheel,wheelFresh,adcsFresh,bench);
    document.querySelectorAll('[data-value="BENCH_ARM"]').forEach(btn=>btn.disabled=Boolean(startBlock));
    document.querySelectorAll('[data-bench-start-status]').forEach(el=>{
      el.textContent=startBlock||(bench?.armed?'Armado bidireccional confirmado.':'Listo para iniciar el armado bidireccional.');
    });
    renderBrakeBatch(bench,maneuverReady,startBlock);
    void pollBrakeBatch();
    document.querySelector('[data-value="DETUMBLE"]').disabled=!maneuverReady||Math.abs(wheel.body_rpm)>=475/6;
    document.querySelector('[data-prefix="TURN"]').disabled=!maneuverReady||Math.abs(wheel.body_rpm)>0.34;
    document.querySelector('[data-prefix="LIGHT"]').disabled=!maneuverReady||wheel.axis!==2||Math.abs(wheel.body_rpm)>0.34;
    document.getElementById('maneuver-turn').textContent=adcsFresh&&wheel.maneuver===2?
      fmt(wheel.rotation_deg,'°',1)+' / '+fmt(wheel.target_deg,'°',1):'—';
    renderAzimuth(adcsFresh?wheel:null);
    document.getElementById('adcs-export').disabled=!data.adcs_history?.length;
    const wheelCmd=(data.commands||[]).filter(cmd=>cmd.name==='WHEEL'&&!cmd.detail.startsWith('BRAKE,')).at(-1);
    if(wheelCmd)document.getElementById('wheel-command-response').textContent=
      '#'+wheelCmd.id+' '+wheelCmd.state+' · '+wheelCmd.detail;
    const relayLabel={ON:'Encendida',OFF:'Apagada',PENDING:'Esperando respuesta'};
    document.getElementById('relay-status').textContent=
      'Salida del relé: '+(relayLabel[data.relay]||'Sin confirmación');
    document.getElementById('command-hint').textContent=enabled?
      'Pico READY: puedes enviar comandos. Solo ACK de STM32 confirma su ejecución.':
      'Controles bloqueados: se requiere PICO_READY,P2P_CMD_V1. Un Pico RX-only no transmite comandos.';
    document.getElementById('pico-logs').textContent=data.logs?.length?
      data.logs.map(log=>'['+log.t+'] '+log.text).join('\n'):'Sin actividad del Pico.';
    for(const [id,keys,colors] of chartDefs) drawChart(id,data.history||[],keys,colors);
    if(data.commands?.length){
      const cmd=data.commands.at(-1);
      document.getElementById('command-response').textContent=
        '#'+cmd.id+' '+cmd.name+(cmd.operator?' · '+cmd.operator:'')+': '+cmd.state+' — '+cmd.detail;
    }
  }
  async function pollPico(){
    try{const resp=await (window.stationRequest || fetch)('/api/pico',{cache:'no-store'});if(!resp.ok)throw Error('HTTP '+resp.status);renderPico(await resp.json());}
    catch(e){
      picoState={...picoState,connected:false};
      updateWheelSetpoint();
      renderSolar(picoState);
      document.getElementById('pico-status-1').textContent='Sin conexión con backend';
      document.getElementById('relay-status').textContent='Salida del relé: Sin confirmación';
      document.getElementById('wheel-state').textContent='Sin conexión con el servidor';
      document.getElementById('adcs-result').textContent='Sin conexión con el servidor';
      document.querySelectorAll('[data-bench-start-status]').forEach(el=>el.textContent='Sin conexión con el panel. Comprueba el enlace al Pico.');
      renderMissionScene(null,false);
      document.querySelectorAll('[data-cmd="WHEEL"]').forEach(btn=>btn.disabled=true);
    }
  }
  document.querySelectorAll('[data-cmd]').forEach(btn=>btn.addEventListener('click',async()=>{
    if(!picoState.connected||!picoState.capable)return;
    if(btn.dataset.confirm==='true'&&!window.confirm('¿Enviar '+btn.dataset.cmd+' a la STM32 vía LoRa? La aplicación no confirmará su aplicación hasta recibir un ACK.'))return;
    let args=btn.dataset.value?[btn.dataset.value]:
      btn.dataset.arg?[document.getElementById(btn.dataset.arg).value]:[];
    const msg=document.getElementById(btn.dataset.cmd==='WHEEL'?'wheel-command-response':'command-response');
    if(btn.dataset.arg&&!document.getElementById(btn.dataset.arg).reportValidity())return;
    if(btn.dataset.prefix){
      const prefix=btn.dataset.prefix==='LIGHT'&&document.getElementById('light-invert').checked?
        'LIGHTINV':btn.dataset.prefix;
      args=[prefix+(prefix==='AXIS_'?'':':')+args[0]];
    }
    msg.textContent='Enviando solicitud...';
    if(lanFeedback)lanFeedback.textContent='';
    try{
      const r=await (window.stationRequest || fetch)('/api/pico/command',commandRequest({command:btn.dataset.cmd,args}));
      const x=await r.json();if(!r.ok)throw Error(x.error||'No se pudo encolar');
      if(btn.dataset.value==='BENCH_ARM'||btn.dataset.prefix==='RUNS')benchStartId=x.id;
      if(btn.dataset.prefix==='LIGHT')lightMeasurement=null;
      msg.textContent='#'+x.id+' en cola; esperando respuesta de la STM32 (NO aplicado todavía)';
    }catch(err){
      msg.textContent='Error: '+err.message;
      if(lanFeedback)lanFeedback.textContent=err.message;
    }
  }));
  document.getElementById('light-invert').addEventListener('change',()=>{
    const wheel=picoState.wheel;
    renderMissionScene(wheel,Boolean(picoState.connected&&picoState.capable&&wheel&&wheel.age<=25));
  });
  document.getElementById('adcs-export').addEventListener('click',()=>{
    const rows=picoState.adcs_history||[];
    if(!rows.length)return;
    const keys=['t','axis','inverted','body_rpm','elapsed_s','detumble','pulse','level','rpm','leds',
      'gyro_calibrated','mag_calibrated','mag_valid','fusion_9dof','light_1','light_2','light_3','light_4',
      'maneuver','rotation_deg','target_deg','reference_deg','light_azimuth_deg','brake_initial_rpm','brake_to_5rpm_s'];
    const csv=[keys.join(','),...rows.map(row=>keys.map(key=>row[key]??'').join(','))].join('\n');
    const uri=URL.createObjectURL(new Blob([csv],{type:'text/csv;charset=utf-8'}));
    const link=document.createElement('a');link.href=uri;link.download='adcs-telemetria.csv';link.click();
    setTimeout(()=>URL.revokeObjectURL(uri),1000);
  });
  pollPico();setInterval(pollPico,1500);
  setInterval(updateWheelSetpoint,250);
})();
