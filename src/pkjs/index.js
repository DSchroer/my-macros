var HISTORY_KEY = 'macroHistory';
var MAX_DAYS = 366;

// Mirrors the goals in src/c/my-macros.c; water is tenths of a liter.
var GOALS = { p: 200, c: 560, f: 100, w: 42 };
GOALS.kcal = (GOALS.p + GOALS.c) * 4 + GOALS.f * 9;

function loadHistory() {
  try {
    return JSON.parse(localStorage.getItem(HISTORY_KEY)) || {};
  } catch (e) {
    return {};
  }
}

function saveHistory(history) {
  var keys = Object.keys(history).sort();
  while (keys.length > MAX_DAYS) {
    delete history[keys.shift()];
  }
  localStorage.setItem(HISTORY_KEY, JSON.stringify(history));
}

Pebble.addEventListener('ready', function() {
  // Ping the watch so it re-sends today's totals in case earlier sends were missed.
  Pebble.sendAppMessage({ DAY: 0 }, null, null);
});

Pebble.addEventListener('appmessage', function(e) {
  var p = e.payload;
  if (!p.DAY) {
    return;
  }
  var history = loadHistory();
  history[String(p.DAY)] = [p.PROTEIN || 0, p.CARBS || 0, p.FAT || 0, p.WATER || 0];
  saveHistory(history);
});

Pebble.addEventListener('showConfiguration', function() {
  Pebble.openURL('data:text/html;charset=utf-8,' + encodeURIComponent(buildPage(loadHistory())));
});

Pebble.addEventListener('webviewclosed', function() {});

function buildPage(history) {
  return '<!DOCTYPE html><html><head>' +
    '<meta name="viewport" content="width=device-width, initial-scale=1">' +
    '<title>My Macros</title>' +
    '<style>' +
    'body{font-family:-apple-system,Roboto,sans-serif;background:#1e1e1e;color:#eee;margin:0;padding:12px}' +
    'h1{font-size:20px;margin:4px 0 12px}' +
    '.tabs{display:flex;margin-bottom:12px}' +
    '.tabs button{flex:1;padding:10px 0;border:0;background:#333;color:#aaa;font-size:15px}' +
    '.tabs button.on{background:#f60;color:#fff}' +
    '.tabs button:first-child{border-radius:6px 0 0 6px}' +
    '.tabs button:last-child{border-radius:0 6px 6px 0}' +
    '.card{background:#2a2a2a;border-radius:8px;padding:10px 12px;margin-bottom:10px}' +
    '.card h2{font-size:15px;margin:0 0 2px}' +
    '.kcal{font-size:13px;color:#7c4;margin-bottom:6px}' +
    '.row{display:flex;align-items:center;font-size:12px;margin:3px 0}' +
    '.row .n{width:52px;color:#aaa}' +
    '.row .v{width:88px;text-align:right;margin-left:6px}' +
    '.bar{flex:1;height:8px;background:#444;border-radius:4px;overflow:hidden}' +
    '.bar div{height:100%;border-radius:4px}' +
    '.empty{color:#888;text-align:center;padding:30px 0}' +
    '.done{display:block;width:100%;padding:12px 0;margin-top:8px;border:0;border-radius:6px;background:#f60;color:#fff;font-size:16px}' +
    '</style></head><body>' +
    '<h1>Macro History</h1>' +
    '<div class="tabs">' +
    '<button id="tab-day" onclick="show(\'day\')">Day</button>' +
    '<button id="tab-week" onclick="show(\'week\')">Week</button>' +
    '<button id="tab-month" onclick="show(\'month\')">Month</button>' +
    '</div>' +
    '<div id="list"></div>' +
    '<button class="done" onclick="location.href=\'pebblejs://close\'">Done</button>' +
    '<script>' +
    'var HISTORY=' + JSON.stringify(history) + ';' +
    'var GOALS=' + JSON.stringify(GOALS) + ';' +
    'var COLORS={p:"#e33",c:"#39f",f:"#fa0",w:"#0cf"};' +
    'var MONTHS=["Jan","Feb","Mar","Apr","May","Jun","Jul","Aug","Sep","Oct","Nov","Dec"];' +
    'var DAYS=["Sun","Mon","Tue","Wed","Thu","Fri","Sat"];' +
    'function toDate(k){return new Date(+k.slice(0,4),+k.slice(4,6)-1,+k.slice(6,8));}' +
    'function fmtW(t){return (t/10).toFixed(1);}' +
    'function kcal(e){return (e[0]+e[1])*4+e[2]*9;}' +
    'function dayLabel(k){var d=toDate(k);return DAYS[d.getDay()]+" "+MONTHS[d.getMonth()]+" "+d.getDate()+", "+d.getFullYear();}' +
    'function weekKey(k){var d=toDate(k);d.setDate(d.getDate()-((d.getDay()+6)%7));' +
    'return d.getFullYear()*10000+(d.getMonth()+1)*100+d.getDate()+"";}' +
    'function weekLabel(k){var d=toDate(k);return "Week of "+MONTHS[d.getMonth()]+" "+d.getDate()+", "+d.getFullYear();}' +
    'function monthKey(k){return k.slice(0,6);}' +
    'function monthLabel(k){return MONTHS[+k.slice(4,6)-1]+" "+k.slice(0,4);}' +
    'function group(keyFn){var g={};Object.keys(HISTORY).forEach(function(k){' +
    'var gk=keyFn(k);(g[gk]=g[gk]||[]).push(HISTORY[k]);});return g;}' +
    'function avg(list){var s=[0,0,0,0];list.forEach(function(e){for(var i=0;i<4;i++)s[i]+=e[i];});' +
    'return s.map(function(v){return Math.round(v/list.length);});}' +
    'function bar(name,color,val,goal,text){var pct=goal?Math.min(100,Math.round(val*100/goal)):0;' +
    'return \'<div class="row"><span class="n">\'+name+\'</span><div class="bar">\'+' +
    '\'<div style="width:\'+pct+\'%;background:\'+color+\'"></div></div>\'+' +
    '\'<span class="v">\'+text+\'</span></div>\';}' +
    'function card(title,e,sub){var h=\'<div class="card"><h2>\'+title+\'</h2>\';' +
    'h+=\'<div class="kcal">\'+kcal(e)+\' / \'+GOALS.kcal+\' kcal\'+(sub?\' &middot; \'+sub:\'\')+\'</div>\';' +
    'h+=bar("Protein",COLORS.p,e[0],GOALS.p,e[0]+" / "+GOALS.p+" g");' +
    'h+=bar("Carbs",COLORS.c,e[1],GOALS.c,e[1]+" / "+GOALS.c+" g");' +
    'h+=bar("Fat",COLORS.f,e[2],GOALS.f,e[2]+" / "+GOALS.f+" g");' +
    'h+=bar("Water",COLORS.w,e[3],GOALS.w,fmtW(e[3])+" / "+fmtW(GOALS.w)+" L");' +
    'return h+"</div>";}' +
    'function show(mode){["day","week","month"].forEach(function(m){' +
    'document.getElementById("tab-"+m).className=(m===mode)?"on":"";});' +
    'var html="";' +
    'if(mode==="day"){Object.keys(HISTORY).sort().reverse().forEach(function(k){' +
    'html+=card(dayLabel(k),HISTORY[k]);});}' +
    'else{var g=group(mode==="week"?weekKey:monthKey);' +
    'Object.keys(g).sort().reverse().forEach(function(k){' +
    'html+=card(mode==="week"?weekLabel(k):monthLabel(k),avg(g[k]),' +
    '"avg/day over "+g[k].length+(g[k].length===1?" day":" days"));});}' +
    'document.getElementById("list").innerHTML=html||\'<div class="empty">No history yet. Log some macros on the watch!</div>\';' +
    '}' +
    'show("day");' +
    '</scr' + 'ipt></body></html>';
}
