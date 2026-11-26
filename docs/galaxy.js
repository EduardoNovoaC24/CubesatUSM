const theme = document.getElementById('theme');
const cube = document.getElementById('cubesat');
const stars = document.getElementById('stars');
const tabs = [...document.querySelectorAll('[role="tab"]')];
const motion = matchMedia('(prefers-reduced-motion: reduce)');
let cubeFrame;
let starFrame;

function canvasContext(canvas) {
  const { width, height } = canvas.getBoundingClientRect();
  const scale = Math.min(devicePixelRatio || 1, 2);
  canvas.width = Math.round(width * scale);
  canvas.height = Math.round(height * scale);
  const context = canvas.getContext('2d');
  if (context) context.scale(scale, scale);
  return { context, width, height };
}

function palette() {
  const style = getComputedStyle(document.documentElement);
  return [style.getPropertyValue('--accent').trim(), style.getPropertyValue('--starlight').trim()];
}

const vertices = [[0,-1], [1,-.5], [1,.5], [0,1], [-1,.5], [-1,-.5], [0,0]];
const edges = [[0,1],[1,2],[2,3],[3,4],[4,5],[5,0],[5,6],[6,1],[6,3]];
const particles = edges.flatMap(([from, to]) => Array.from({ length: 24 }, (_, i) => {
  const t = i / 23;
  return {
    x: vertices[from][0] * (1 - t) + vertices[to][0] * t,
    y: vertices[from][1] * (1 - t) + vertices[to][1] * t,
    sx: (Math.random() - .5) * 2,
    sy: (Math.random() - .5) * 2,
    size: .65 + Math.random() * 1.4,
  };
}));

function drawCube(animate) {
  cancelAnimationFrame(cubeFrame);
  const { context, width, height } = canvasContext(cube);
  if (!context) return;
  const colors = palette();
  const radius = Math.min(width, height) * .34;
  const start = performance.now();
  function frame(now) {
    const time = animate && !motion.matches ? Math.min(1, (now - start) / 1250) : 1;
    const progress = 1 - (1 - time) ** 3;
    context.clearRect(0, 0, width, height);
    particles.forEach((particle, i) => {
      const x = width / 2 + particle.sx * width / 2 * (1 - progress) + particle.x * radius * progress;
      const y = height / 2 + particle.sy * height / 2 * (1 - progress) + particle.y * radius * progress;
      context.fillStyle = colors[i % colors.length];
      context.globalAlpha = .35 + progress * .6;
      context.beginPath();
      context.arc(x, y, particle.size, 0, Math.PI * 2);
      context.fill();
    });
    if (time < 1) cubeFrame = requestAnimationFrame(frame);
  }
  cubeFrame = requestAnimationFrame(frame);
}

function animateStars() {
  cancelAnimationFrame(starFrame);
  const { context, width, height } = canvasContext(stars);
  if (!context || motion.matches) return;
  const colors = palette();
  const dots = Array.from({ length: 85 }, () => {
    const angle = Math.random() * Math.PI * 2;
    return { x: Math.cos(angle), y: Math.sin(angle), distance: 20 + Math.random() * Math.max(width, height) * .65 };
  });
  const start = performance.now();
  function frame(now) {
    const time = Math.min(1, (now - start) / 520);
    context.clearRect(0, 0, width, height);
    context.globalAlpha = Math.sin(time * Math.PI) * .65;
    dots.forEach((dot, i) => {
      const distance = (time + .2) * dot.distance;
      context.strokeStyle = colors[i % colors.length];
      context.beginPath();
      context.moveTo(width / 2 + dot.x * distance, height / 2 + dot.y * distance);
      context.lineTo(width / 2 + dot.x * (distance + 12), height / 2 + dot.y * (distance + 12));
      context.stroke();
    });
    if (time < 1) starFrame = requestAnimationFrame(frame);
  }
  starFrame = requestAnimationFrame(frame);
}

function selectTab(tab) {
  if (tab.getAttribute('aria-selected') === 'true') return;
  tabs.forEach((item) => {
    const selected = item === tab;
    item.setAttribute('aria-selected', selected);
    item.tabIndex = selected ? 0 : -1;
    document.getElementById(item.getAttribute('aria-controls')).hidden = !selected;
  });
  animateStars();
}

tabs.forEach((tab, index) => {
  tab.addEventListener('click', () => selectTab(tab));
  tab.addEventListener('keydown', (event) => {
    const next = { ArrowLeft: index - 1, ArrowRight: index + 1, Home: 0, End: tabs.length - 1 }[event.key];
    if (next === undefined) return;
    event.preventDefault();
    const target = tabs[(next + tabs.length) % tabs.length];
    selectTab(target);
    target.focus();
  });
});

try {
  const saved = localStorage.getItem('cubesat-galaxy');
  if (['nebula', 'red', 'green'].includes(saved)) theme.value = saved;
} catch { /* Appearance still works without storage. */ }
document.documentElement.dataset.theme = theme.value;
theme.addEventListener('change', () => {
  document.documentElement.dataset.theme = theme.value;
  try { localStorage.setItem('cubesat-galaxy', theme.value); } catch { /* Optional preference. */ }
  drawCube(true);
});
window.addEventListener('resize', () => drawCube(false));
motion.addEventListener('change', () => {
  drawCube(false);
  if (motion.matches) {
    cancelAnimationFrame(starFrame);
    canvasContext(stars);
  }
});
drawCube(true);
