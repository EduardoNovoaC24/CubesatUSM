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
  let spinStatusAt=0;
  let spinCommandId;
  let turnRequestInFlight=false;
  let turnCommandId;
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
  function renderTimedSpin(wheel, adcsFresh, commands){
    const timed=adcsFresh&&wheel.maneuver===4;
    const motion=timed&&wheel.spin_result===0&&[1,3].includes(wheel.state);
    const braking=timed&&wheel.detumble===1;
    const resultLabels=['Girando','Giro estabilizado.','Desarmado por operador.','Detenido por pérdida de enlace.','Detenido por una lectura de giro inválida.','No se alcanzó el PWM pedido.','Frenado interrumpido: no se estabilizó.'];
    const pending=commands?.find(cmd=>cmd.id===spinCommandId);
    const waiting=spinCommandId!==undefined&&(!pending||['EN COLA','PICO QUEUED','PICO SENT'].includes(pending.state));
    if(spinCommandId!==undefined&&!waiting)spinCommandId=undefined;
    const block=!adcsFresh?'Esperando telemetría reciente de la rueda.':
      !wheel.gyro_calibrated?'Espera la calibración del giroscopio con el CubeSat quieto.':
      wheel.state!==0?'La rueda debe estar desarmada.':
      !Number.isFinite(wheel.body_rpm)?'Esperando una lectura válida del giroscopio.':'';
    document.getElementById('spin-start').disabled=waiting||!picoState.connected||!picoState.capable;
    const brake=document.getElementById('spin-brake');
    const connected=picoState.connected&&picoState.capable;
    brake.disabled=!connected;
    document.getElementById('spin-disarm').disabled=!connected;
    document.getElementById('spin-status').textContent=waiting?'Esperando el ACK del STM32.':
      motion&&wheel.state===1?'Armando el ESC.':
      braking&&wheel.spin_result===1?'Manteniendo el reposo. El control sigue activo.':
      braking?'Frenando con el giroscopio.':
      motion?'Giro temporizado en curso.':
      timed&&wheel.spin_result?resultLabels[wheel.spin_result]||block:
      block||'Listo para iniciar el giro.';
    document.getElementById('spin-elapsed').textContent=timed?fmt(wheel.elapsed_s,' s',1):'—';
    document.getElementById('spin-clock-label').textContent=timed&&wheel.detumble?'Tiempo de frenado':'Tiempo de giro';
    document.getElementById('spin-duration').textContent=timed?fmt(wheel.spin_seconds,' s',0):'—';
    if(motion||braking)void keepTimedLink();
  }
  function renderTurn(wheel, fresh, commands){
    const command=commands?.find(cmd=>cmd.id===turnCommandId);
    const waiting=turnRequestInFlight||(turnCommandId!==undefined&&
      (!command||['EN COLA','PICO QUEUED','PICO SENT'].includes(command.state)));
    if(!waiting)turnCommandId=undefined;
    const available=fresh&&picoState.connected&&picoState.capable&&
      wheel.axis<3&&wheel.gyro_calibrated&&Number.isFinite(wheel.body_rpm);
    const active=fresh&&wheel.maneuver===2&&wheel.detumble===1;
    const turn=fresh&&wheel.maneuver===2&&wheel.target_deg===180;
    const ready=available&&wheel.state===2&&wheel.profile===2;
    document.getElementById('turn-arm').disabled=waiting||!available||
      wheel.state!==0||Math.abs(wheel.body_rpm)>0.08;
    document.getElementById('turn-start').disabled=waiting||!ready||Math.abs(wheel.body_rpm)>0.33;
    document.getElementById('turn-angle').textContent=turn?fmt(wheel.rotation_deg,'°',1):'—';
    document.getElementById('turn-error').textContent=turn&&Number.isFinite(wheel.rotation_deg)?
      fmt(wheel.target_deg-wheel.rotation_deg,'°',1):'—';
    const results={2:'La STM32 confirm? reposo dentro de la tolerancia.',3:'Tiempo límite alcanzado.',
      4:'Lectura de giro inválida.',5:'Giro detenido.',6:'Límite de la rueda alcanzado.',
      7:'El giro se alej? del objetivo.',8:'Se perdi? el enlace.'};
    document.getElementById('turn-status').textContent=waiting?'Esperando confirmación de la STM32.':
      !fresh?'Esperando telemetría de la rueda.':
      active?'Girando hacia 180°.':
      wheel.state===1?'Armando el ESC.':
      ready?'Rueda armada. Pulsa Girar 180° con el CubeSat quieto.':
      turn&&results[wheel.detumble]?results[wheel.detumble]:
      !available?'Conecta el Pico y verifica el eje y la calibración del giroscopio.':
      'Con el CubeSat quieto, pulsa Armar para giro.';
    if(active||(fresh&&wheel.state===2))void keepTimedLink();
  }
  async function keepTimedLink(){
    if((lanKey&&!lanKey.value.trim())||performance.now()-spinStatusAt<8000)return;
    spinStatusAt=performance.now();
    try{
      await (window.stationRequest||fetch)('/api/pico/command',commandRequest({command:'WHEEL',args:['STATUS']}));
    }catch{ /* The STM32 stops independently if the link is lost. */ }
  }
  const chartDefs=[['plot-q',['q0','q1','q2','q3'],['#f47588','#4de7ae','#ffd37d','#c7a3ff']],
                   ['plot-norm',['norm'],['#4fe5c0']],
                   ['plot-acc',['ax','ay','az'],['#f47588','#4de7ae','#ffd37d']],
                   ['plot-gyro',['gx','gy','gz'],['#f47588','#4de7ae','#ffd37d']],
                   ['plot-mag',['mx','my','mz'],['#f47588','#4de7ae','#ffd37d']]];
  function renderMissionScene(wheel, adcsFresh){
    document.querySelectorAll('[data-mission-light]').forEach(sensor=>{
      const face=sensor.dataset.missionLight;
      const value=adcsFresh?wheel['light_'+face]:null;
      const valid=Number.isFinite(value);
      const signal=valid?value:0;
      sensor.dataset.state=valid?'measured':'unknown';
      sensor.style.setProperty('--signal',String(Math.min(signal,1023)/1023));
      document.querySelector('[data-light-value="'+face+'"]').textContent=valid?value+' ADC':'—';
      const meter=document.querySelector('[data-light-meter="'+face+'"]');
      meter.hidden=!valid;
      meter.value=signal;
    });
    document.getElementById('mission-rotor').dataset.measured=
      String(adcsFresh&&Number.isFinite(wheel.rpm)&&wheel.rpm>0);
  }
  function renderPanels(data){
    const sample=data.panels;
    const legacy=!sample&&data.connected&&data.test_panel?.age<=30;
    const channels=data.connected&&sample?.age<=45?sample.channels:legacy?[data.test_panel]:[];
    document.querySelectorAll('[data-panel-voltage]').forEach(el=>{
      const channel=channels[Number(el.dataset.panelVoltage)];
      el.textContent=channel?.voltage!=null?
        (channel.saturated?'≥ ':'')+fmt(channel.voltage,' V',2):'—';
    });
    document.getElementById('panels-status').textContent=channels.some(channel=>channel.saturated)?
      'Una entrada ADC está saturada. Revisa el divisor.':
      legacy?'Trama de un canal recibida · PC4':
      channels.some(channel=>channel.voltage!=null)?'Última captura hace '+Math.round(sample.age)+' s':
      'Sin voltajes recientes del STM32';
    drawChart('plot-solar',data.panel_history||[],['panel1','panel2','panel3'],
      ['#f6b55b','#ee8394','#81caff']);
  }
  function renderSolar(data){
    renderPanels(data);
    const reading=data.solar;
    const fresh=data.connected&&reading&&reading.age<=30;
    const ina=reading?.source==='ina219';
    document.querySelectorAll('[data-eps-analog-only]').forEach(el=>el.hidden=ina);
    document.getElementById('eps-solar-label').textContent=ina?'Línea solar · INA219':'Pareja 1 · PC4';
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
    const adcsFresh=wheelFresh&&wheel.detumble!=null;
    renderMissionScene(wheel,adcsFresh);
    document.getElementById('adcs-axis').textContent=adcsFresh&&wheel.axis<3?
      'XYZ'[wheel.axis]+(wheel.inverted?'−':'+'):'—';
    document.getElementById('adcs-rate').textContent=adcsFresh?fmt(wheel.body_rpm,'',2):'—';
    document.getElementById('adcs-sensors').textContent=adcsFresh?
      (wheel.gyro_calibrated?'Giro calibrado':'Giro sin calibrar')+' · '+
      (wheel.fusion_9dof?'Fusión 9DOF':'Fusión 6DOF'):'Esperando datos del GY‑91';
    renderTimedSpin(wheel,adcsFresh,data.commands);
    renderTurn(wheel,adcsFresh,data.commands);
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
      renderSolar(picoState);
      document.getElementById('pico-status-1').textContent='Sin conexión con backend';
      document.getElementById('relay-status').textContent='Salida del relé: Sin confirmación';
      document.getElementById('wheel-state').textContent='Sin conexión con el servidor';
      document.getElementById('spin-status').textContent='Sin conexión con el panel.';
      renderMissionScene(null,false);
      document.getElementById('turn-status').textContent='Sin conexión con el panel.';
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
    if(btn.dataset.prefix==='RUN'){
      const pulse=document.getElementById('spin-pwm');
      const duration=document.getElementById('spin-seconds');
      if(!pulse.reportValidity()||!duration.reportValidity())return;
      args=['RUN:'+pulse.valueAsNumber+':'+duration.valueAsNumber];
      btn.disabled=true;
    }else if(btn.dataset.prefix==='AXIS_'){
      args=['AXIS_'+args[0]];
    }
    const turnRequest=btn.id==='turn-arm'||btn.id==='turn-start';
    if(turnRequest){
      if(turnRequestInFlight||turnCommandId!==undefined)return;
      turnRequestInFlight=true;
      btn.disabled=true;
    }
    msg.textContent='Enviando solicitud...';
    if(lanFeedback)lanFeedback.textContent='';
    try{
      const r=await (window.stationRequest || fetch)('/api/pico/command',commandRequest({command:btn.dataset.cmd,args}));
      const x=await r.json();if(!r.ok)throw Error(x.error||'No se pudo encolar');
      if(btn.dataset.prefix==='RUN'){spinCommandId=x.id;spinStatusAt=-Infinity;}
      if(turnRequest){turnCommandId=x.id;spinStatusAt=-Infinity;}
      msg.textContent='#'+x.id+' en cola; esperando respuesta de la STM32 (NO aplicado todavía)';
    }catch(err){
      msg.textContent='Error: '+err.message;
      if(lanFeedback)lanFeedback.textContent=err.message;
    }finally{
      if(turnRequest)turnRequestInFlight=false;
    }
  }));
  document.getElementById('adcs-export').addEventListener('click',()=>{
    const rows=picoState.adcs_history||[];
    if(!rows.length)return;
    const keys=['t','axis','inverted','body_rpm','elapsed_s','detumble','pulse','level','rpm','leds',
      'gyro_calibrated','mag_calibrated','mag_valid','fusion_9dof','light_1','light_2','light_3','light_4',
      'maneuver','rotation_deg','target_deg','reference_deg','light_azimuth_deg','brake_initial_rpm','brake_to_5rpm_s','spin_seconds','spin_pwm','spin_result'];
    const csv=[keys.join(','),...rows.map(row=>keys.map(key=>row[key]??'').join(','))].join('\n');
    const uri=URL.createObjectURL(new Blob([csv],{type:'text/csv;charset=utf-8'}));
    const link=document.createElement('a');link.href=uri;link.download='adcs-telemetria.csv';link.click();
    setTimeout(()=>URL.revokeObjectURL(uri),1000);
  });
  pollPico();setInterval(pollPico,1500);
})();
