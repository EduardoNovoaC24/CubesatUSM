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
    wheelSetpoint.disabled=!(seconds>0||manualRunning);
    wheelCountdown.hidden=!ready;
    wheelCountdown.textContent=seconds>0
      ? 'Lista. Tiempo estimado para aplicar el nivel '+seconds+' s.'
      : 'Plazo vencido. Espera a que la rueda vuelva a Desarmada.';
  }
  const fmt=(v,unit='',places=0)=>typeof v==='number'&&Number.isFinite(v)?v.toFixed(places)+unit:'—';
  // CSS matrix3d for quaternion rotation. Missing data stays in neutral pose.
  function modelQuaternion(r){
    if(!r||![r.q0,r.q1,r.q2,r.q3].every(v=>typeof v==='number')) return;
    let [w,x,y,z]=[r.q0,r.q1,r.q2,r.q3];
    const norm=Math.hypot(w,x,y,z);if(norm<.001)return;
    [w,x,y,z]=[w/norm,x/norm,y/norm,z/norm];
    const m=[1-2*(y*y+z*z),2*(x*y+z*w),2*(x*z-y*w),0,
             2*(x*y-z*w),1-2*(x*x+z*z),2*(y*z+x*w),0,
             2*(x*z+y*w),2*(y*z-x*w),1-2*(x*x+y*y),0,
             0,0,0,1];
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
    if(latest?.mag_detected===false) magStatus='No detectado';
    if(latest?.mag_detected===true){
      if(!latest.mag_valid) magStatus='Detectado · sin muestra válida';
      else magStatus=latest.mag_calibrated?'Con datos · calibrado':'Con datos · sin calibrar';
    }
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
    document.querySelectorAll('[data-test-result]').forEach(el=>{
      const status=activeTest===Number(el.dataset.testResult)?results[wheel.detumble]:
        adcsFresh?'Sin prueba':'Sin telemetría ADCS reciente';
      if(el.textContent!==status)el.textContent=status;
    });
    document.getElementById('adcs-sensors').textContent=adcsFresh?
      (wheel.gyro_calibrated?'Giro calibrado':'Giro sin calibrar')+' · '+
      (wheel.mag_valid?(wheel.mag_calibrated?'Mag calibrado':'Mag sin calibrar'):'Mag sin dato válido')+
      ' · '+(wheel.fusion_9dof?'Fusión 9DOF':'Fusión 6DOF'):'Esperando datos del GY‑91';
    document.querySelectorAll('[data-adcs-cal]').forEach(el=>{
      el.disabled=!(adcsFresh&&wheel.state===0&&(el.dataset.cmd!=='SET_MODE'||(wheel.mag_calibrated&&wheel.mag_valid)));
    });
    document.querySelector('[data-prefix="AXIS_"]').disabled=!wheelIdle;
    const maneuverReady=adcsFresh&&wheel.profile===2&&wheel.state===2&&wheel.axis<3&&
      Number.isFinite(wheel.body_rpm);
    document.querySelector('[data-value="DETUMBLE"]').disabled=!maneuverReady;
    document.querySelector('[data-prefix="TURN"]').disabled=!maneuverReady||Math.abs(wheel.body_rpm)>0.34;
    document.querySelector('[data-prefix="LIGHT"]').disabled=!maneuverReady||wheel.axis!==2||Math.abs(wheel.body_rpm)>0.34;
    document.getElementById('maneuver-turn').textContent=adcsFresh&&wheel.maneuver===2?
      fmt(wheel.rotation_deg,'°',1)+' / '+fmt(wheel.target_deg,'°',1):'—';
    document.getElementById('maneuver-reference').textContent=adcsFresh?fmt(wheel.reference_deg,'°',1):'—';
    document.getElementById('maneuver-light').textContent=adcsFresh?fmt(wheel.light_azimuth_deg,'°',1):'—';
    document.getElementById('adcs-export').disabled=!data.adcs_history?.length;
    const wheelCmd=(data.commands||[]).filter(cmd=>cmd.name==='WHEEL').at(-1);
    if(wheelCmd)document.getElementById('wheel-command-response').textContent=
      '#'+wheelCmd.id+' '+wheelCmd.state+' · '+wheelCmd.detail;
    const calCmd=(data.commands||[]).filter(cmd=>['CAL_GYRO','MAG_START','MAG_STOP','SET_MODE'].includes(cmd.name)).at(-1);
    if(calCmd)document.getElementById('adcs-cal-response').textContent=
      '#'+calCmd.id+' '+calCmd.state+' · '+calCmd.detail;
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
        '#'+cmd.id+' '+cmd.name+': '+cmd.state+' — '+cmd.detail;
    }
  }
  async function pollPico(){
    try{const resp=await fetch('/api/pico',{cache:'no-store'});if(!resp.ok)throw Error('HTTP '+resp.status);renderPico(await resp.json());}
    catch(e){
      picoState={...picoState,connected:false};
      updateWheelSetpoint();
      renderSolar(picoState);
      document.getElementById('pico-status-1').textContent='Sin conexión con backend';
      document.getElementById('relay-status').textContent='Salida del relé: Sin confirmación';
      document.getElementById('wheel-state').textContent='Sin conexión con el servidor';
      document.getElementById('adcs-result').textContent='Sin conexión con el servidor';
      renderMissionScene(null,false);
      document.querySelectorAll('[data-cmd="WHEEL"],[data-adcs-cal]').forEach(btn=>btn.disabled=true);
    }
  }
  document.querySelectorAll('[data-cmd]').forEach(btn=>btn.addEventListener('click',async()=>{
    if(!picoState.connected||!picoState.capable)return;
    if(btn.dataset.confirm==='true'&&!window.confirm('¿Enviar '+btn.dataset.cmd+' a la STM32 vía LoRa? La aplicación no confirmará su aplicación hasta recibir un ACK.'))return;
    let args=btn.dataset.value?[btn.dataset.value]:
      btn.dataset.arg?[document.getElementById(btn.dataset.arg).value]:[];
    const msg=document.getElementById(btn.dataset.cmd==='WHEEL'?'wheel-command-response':
      btn.hasAttribute('data-adcs-cal')?'adcs-cal-response':'command-response');
    if(btn.dataset.arg&&!document.getElementById(btn.dataset.arg).reportValidity())return;
    if(btn.dataset.prefix){
      const prefix=btn.dataset.prefix==='LIGHT'&&document.getElementById('light-invert').checked?
        'LIGHTINV':btn.dataset.prefix;
      args=[prefix+(prefix==='AXIS_'?'':':')+args[0]];
    }
    msg.textContent='Enviando solicitud...';
    try{
      const r=await fetch('/api/pico/command',{method:'POST',headers:{'Content-Type':'application/json'},body:JSON.stringify({command:btn.dataset.cmd,args})});
      const x=await r.json();if(!r.ok)throw Error(x.error||'No se pudo encolar');
      msg.textContent='#'+x.id+' en cola; esperando respuesta de la STM32 (NO aplicado todavía)';
    }catch(err){msg.textContent='Error: '+err.message;}
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
      'maneuver','rotation_deg','target_deg','reference_deg','light_azimuth_deg'];
    const csv=[keys.join(','),...rows.map(row=>keys.map(key=>row[key]??'').join(','))].join('\n');
    const uri=URL.createObjectURL(new Blob([csv],{type:'text/csv;charset=utf-8'}));
    const link=document.createElement('a');link.href=uri;link.download='adcs-telemetria.csv';link.click();
    setTimeout(()=>URL.revokeObjectURL(uri),1000);
  });
  pollPico();setInterval(pollPico,1500);
  setInterval(updateWheelSetpoint,250);
})();
