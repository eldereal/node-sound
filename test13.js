// bufferifies in a loop to detect leaks.

var Sound= require('./build/default/sound');
var sounds= ['sound.wav',
             'Sous La Pluie.mp3',
             'sound.m4a',
             'Sous La Pluie.mp3',
             'sound.aif',
             'Sous La Pluie.mp3',
             'sound.mp3',
             'Sous La Pluie.mp3',
             'sound.au'
];

var i= sounds.length;
(function next () {
  if (i--) {
    var len= Sound.bufferifySync(sounds[i]).length;
    process.stdout.write('\n******************************* '+ sounds[i]+ " -> "+ (len/(1024*1024)).toFixed(1)+ 'MB');
  }
  else i= sounds.length;
  process.nextTick(next);
})();
