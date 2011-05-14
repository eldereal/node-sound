// exercise callbacks;

var Sound= require('./build/default/sound');

var quit= setTimeout(function nop(){}, 1e9); // don't quit

var howMany= 999;

function cb () {
  process.stdout.write('\n['+ this.id+ '].callback');
}


var buffer= new Buffer(44100*4);
var i= buffer.length;
while (i--) buffer[i]= 0;
Sound.create(buffer).loop(1e9).play();


var buffer= new Buffer(2048);
var i= buffer.length;
while (i--) buffer[i]= Math.floor(256*Math.random());


var i= howMany;
function next () {
  var snd= Sound.create(buffer);
  process.stdout.write('\n['+ snd.id+ '].play(cb)');
  snd.play(cb);
  snd= null;
  if (--i) setTimeout(next, Math.floor(10*Math.random()));
};

next();