var Sound= require('./build/default/sound');

var sounds= ['sound.wav', 'sound.m4a', 'sound.aif', 'sound.mp3', 'sound.au', 'Sous La Pluie.mp3'];

var i= 0;
(function next () {
  if (++i < sounds.length) {
    process.stdout.write('\n******************************* '+ sounds[i]+ " ->");
    console.log(" -> buffer.length -> "+ Sound.create(Sound.bufferify(sounds[i])).play(next).data.length);
  }
  else {
    clearTimeout(quitTimer);
  }
})();

quitTimer= setTimeout(Date.now, 1e9);