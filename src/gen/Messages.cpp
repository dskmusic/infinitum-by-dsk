#include "Messages.h"
#include <map>

namespace gx
{
namespace
{
using List = std::vector<const char*>;

const List genericEs {
    "Despertando a los osciladores…", "Calentando válvulas imaginarias…", "Afinando el caos hasta que suene bonito…",
    "Negociando con el filtro… ya casi.", "Pidiendo permiso a la resonancia…", "Contando armónicos con los dedos…",
    "Barajando formas de onda…", "Poniendo la envolvente a dieta…", "Convenciendo al LFO de que se mueva con estilo…",
    "Alineando planetas y fases…", "Ajustando la proporción áurea del detune…", "Puliendo transitorios con cariño…",
    "Consultando el manual que nadie lee…", "Esquivando el clipping por los pelos…", "Midiendo el headroom con regla…",
    "Mezclando ciencia y un poco de magia…", "Rebobinando el universo un semitono…", "Dando cuerda al secuenciador…",
    "Desenredando cables virtuales…", "Echando sal y pimienta al espectro…", "Probando el sonido en tres salas distintas…",
    "Domando frecuencias salvajes…", "Recalculando la gravedad del grave…", "Buscando el punto dulce del cutoff…",
    "Hablando bajito con el compresor…", "Cruzando genes sonoros…", "Mutando parciales con buen gusto…",
    "Eliminando el DC que se coló…", "Revisando que nada pite raro…", "Aplicando teoría musical seria (casi)…",
    "Ensamblando el patch pieza a pieza…", "Desmontando el preset anterior con cuidado…", "Ordenando la cadena de efectos…",
    "Sincronizando con el reloj del host…", "Inspirándose en sintes legendarios…", "Echando un ojo al osciloscopio…",
    "Escuchando con oídos de murciélago…", "Ajustando la reverb para que no moleste al vecino…", "Calibrando el factor wow…",
    "Pasando la validación de calidad… ✔", "Comprobando que no haya silencios incómodos…", "Pidiendo una segunda opinión al analizador…" };

const List genericEn {
    "Waking up the oscillators…", "Warming up imaginary tubes…", "Tuning the chaos until it sounds nice…",
    "Negotiating with the filter… almost there.", "Asking the resonance for permission…", "Counting harmonics on my fingers…",
    "Shuffling waveforms…", "Putting the envelope on a diet…", "Teaching the LFO to move with style…",
    "Aligning planets and phases…", "Setting the golden ratio of detune…", "Polishing transients with love…",
    "Reading the manual nobody reads…", "Dodging clipping by a hair…", "Measuring headroom with a ruler…",
    "Mixing science with a little magic…", "Rewinding the universe by a semitone…", "Winding up the sequencer…",
    "Untangling virtual cables…", "Seasoning the spectrum…", "Trying the sound in three different rooms…",
    "Taming wild frequencies…", "Recalculating the gravity of the bass…", "Hunting for the cutoff sweet spot…",
    "Whispering to the compressor…", "Crossing sonic genes…", "Mutating partials tastefully…",
    "Removing the DC that sneaked in…", "Making sure nothing beeps weirdly…", "Applying serious music theory (mostly)…",
    "Assembling the patch piece by piece…", "Carefully taking the last preset apart…", "Sorting the effects chain…",
    "Syncing to the host clock…", "Channelling legendary synths…", "Peeking at the oscilloscope…",
    "Listening with bat ears…", "Keeping the reverb polite for the neighbours…", "Calibrating the wow factor…",
    "Passing quality control… ✔", "Checking for awkward silences…", "Getting a second opinion from the analyser…" };

const std::map<juce::String, std::pair<List, List>> byCategory {
    { "Bass", { { "Consultando a las musas del bajo…", "Bajando el sub hasta el sótano…", "Midiendo cuánto tiembla el suelo…", "Engrasando el motor del grave…",
                  "Pidiendo al woofer que aguante…", "Cerrando el filtro con decisión…", "Añadiendo un poco de mugre buena…", "Centrando el grave en mono…",
                  "Ajustando el pegamento del sub…", "Dando peso a cada nota…", "Afilando el ataque para que pegue…", "Preparando el terremoto controlado…" },
                { "Consulting the muses of bass…", "Lowering the sub into the basement…", "Measuring how much the floor shakes…", "Greasing the low-end engine…",
                  "Asking the woofer to hold on…", "Closing the filter with conviction…", "Adding a little good dirt…", "Keeping the low end in mono…",
                  "Tuning the sub glue…", "Giving every note some weight…", "Sharpening the attack so it hits…", "Preparing a controlled earthquake…" } } },
    { "Lead", { { "Encendiendo el foco del solista…", "Afilando la sierra principal…", "Ensayando un solo épico…", "Añadiendo vibrato con sentimiento…",
                  "Puliendo el brillo para que corte la mezcla…", "Calentando el glide…", "Dándole voz al oscilador…", "Preparando la nota que todos recordarán…",
                  "Subiendo al escenario…", "Domando la sincronía de osciladores…", "Puliendo el delay para el estribillo…", "Ajustando el carisma…" },
                { "Turning on the soloist spotlight…", "Sharpening the main saw…", "Rehearsing an epic solo…", "Adding vibrato with feeling…",
                  "Polishing the shine so it cuts through…", "Warming up the glide…", "Giving the oscillator a voice…", "Preparing the note everyone remembers…",
                  "Stepping on stage…", "Taming oscillator sync…", "Polishing the delay for the chorus…", "Adjusting the charisma…" } } },
    { "Pad", { { "Extendiendo una alfombra de nubes…", "Ralentizando el tiempo…", "Respirando hondo con el ataque…", "Abriendo el espacio estéreo…",
                 "Dejando que la reverb se haga grande…", "Haciendo que evolucione sin prisa…", "Desafinando con elegancia…", "Pintando un amanecer sonoro…",
                 "Acolchando cada armónico…", "Flotando entre frames de wavetable…", "Dando calidez a cada voz…", "Llenando la habitación de luz…" },
               { "Rolling out a carpet of clouds…", "Slowing down time…", "Taking a deep breath with the attack…", "Opening up the stereo field…",
                 "Letting the reverb grow big…", "Making it evolve, no rush…", "Detuning gracefully…", "Painting a sonic sunrise…",
                 "Cushioning every harmonic…", "Floating between wavetable frames…", "Warming every voice…", "Filling the room with light…" } } },
    { "Pluck", { { "Pellizcando cuerdas imaginarias…", "Recortando el decay a medida…", "Haciendo que cada nota rebote…", "Sacando brillo al transitorio…",
                   "Afinando gotas de cristal…", "Midiendo el eco de cada pulsación…", "Dando el punto justo de chispa…", "Ajustando el tic-tac del delay…",
                   "Haciendo que suene a madera y a luz…", "Puliendo la envolvente del filtro…", "Contando milisegundos con lupa…", "Preparando la lluvia de notas…" },
                 { "Plucking imaginary strings…", "Trimming the decay to size…", "Making every note bounce…", "Polishing the transient…",
                   "Tuning drops of glass…", "Measuring the echo of every pluck…", "Adding just the right spark…", "Setting the delay tick-tock…",
                   "Making it sound like wood and light…", "Polishing the filter envelope…", "Counting milliseconds with a magnifier…", "Preparing a rain of notes…" } } },
    { "Keys", { { "Limpiando el polvo de las teclas…", "Afinando los martillos…", "Poniendo el chorus de domingo…", "Calentando el amplificador vintage…",
                  "Ajustando la respuesta a la velocidad…", "Sentando al pianista…", "Dando un toque de club nocturno…", "Puliendo las campanitas…",
                  "Balanceando el ataque y el cuerpo…", "Activando el modo jam session…", "Sirviendo un acorde con jazz…", "Afinando el carácter de los tines…" },
                { "Dusting off the keys…", "Tuning the hammers…", "Switching on the Sunday chorus…", "Warming up the vintage amp…",
                  "Adjusting velocity response…", "Seating the pianist…", "Adding a late-night club touch…", "Polishing the little bells…",
                  "Balancing attack and body…", "Entering jam session mode…", "Serving a chord with some jazz…", "Tuning the character of the tines…" } } },
    { "Arp", { { "Dando cuerda al arpegiador…", "Ordenando las notas por altura…", "Programando pasos con ritmo…", "Añadiendo ratchets con picardía…",
                 "Sincronizando el delay al tempo…", "Contando semicorcheas…", "Probando probabilidades por paso…", "Haciendo bailar al patrón…",
                 "Subiendo octavas en escalera…", "Ajustando el swing justo…", "Afinando la máquina de secuencias…", "Calibrando el groove…" },
               { "Winding up the arpeggiator…", "Sorting notes by pitch…", "Programming steps with rhythm…", "Adding cheeky ratchets…",
                 "Syncing the delay to tempo…", "Counting sixteenth notes…", "Testing per-step probabilities…", "Making the pattern dance…",
                 "Climbing octaves like stairs…", "Dialling in the swing…", "Tuning the sequence machine…", "Calibrating the groove…" } } },
    { "Strings", { { "Afinando la sección de cuerdas…", "Pasando resina a los arcos…", "Colocando a los músicos en la sala…", "Añadiendo vibrato expresivo…",
                     "Haciendo que el ataque respire…", "Creando un crescendo cinematográfico…", "Abriendo el telón…", "Dando cuerpo a la orquesta virtual…",
                     "Buscando la emoción en cada nota…", "Ensanchando el ensemble…", "Midiendo la acústica de la sala…", "Escribiendo una banda sonora…" },
                   { "Tuning the string section…", "Rosining the bows…", "Seating the musicians in the hall…", "Adding expressive vibrato…",
                     "Letting the attack breathe…", "Building a cinematic crescendo…", "Raising the curtain…", "Giving body to the virtual orchestra…",
                     "Finding the emotion in every note…", "Widening the ensemble…", "Measuring the hall acoustics…", "Writing a soundtrack…" } } },
    { "Brass", { { "Puliendo los metales…", "Inflando los pulmones del sinte…", "Preparando la fanfarria…", "Afinando la sección de vientos…",
                   "Añadiendo mordida al ataque…", "Abriendo el filtro como una trompeta…", "Ensayando un stab funky…", "Dando brillo dorado…",
                   "Sincronizando la sección…", "Calentando las boquillas…", "Ajustando la intensidad del soplo…", "Anunciando la entrada triunfal…" },
                 { "Polishing the brass…", "Filling the synth's lungs…", "Preparing the fanfare…", "Tuning the horn section…",
                   "Adding bite to the attack…", "Opening the filter like a trumpet…", "Rehearsing a funky stab…", "Adding a golden shine…",
                   "Syncing the section…", "Warming up the mouthpieces…", "Adjusting the breath intensity…", "Announcing the grand entrance…" } } },
    { "FX", { { "Abriendo un portal dimensional…", "Calibrando el riser hacia el infinito…", "Recogiendo ruido cósmico…", "Rompiendo cosas con mucho cuidado…",
                "Capturando el sonido de un agujero de gusano…", "Activando el modo ciencia ficción…", "Doblando el espacio-tiempo…", "Cargando el impacto…",
                "Granulando la realidad…", "Interfiriendo transmisiones lejanas…", "Encendiendo la máquina de humo…", "Programando la sorpresa…" },
              { "Opening a dimensional portal…", "Calibrating the riser to infinity…", "Collecting cosmic noise…", "Breaking things very carefully…",
                "Recording the sound of a wormhole…", "Engaging sci-fi mode…", "Bending space-time…", "Charging the impact…",
                "Granulating reality…", "Jamming distant transmissions…", "Turning on the smoke machine…", "Programming the surprise…" } } },
    { "Drums/Perc", { { "Tensando el parche del bombo…", "Afinando la caja con la llave…", "Barriendo el charles…", "Contando hasta cuatro…",
                        "Puliendo el click del transitorio…", "Ajustando la caída de tono…", "Comprobando que pegue en el pecho…", "Preparando el groove…",
                        "Golpeando con precisión de metrónomo…", "Recortando colas innecesarias…", "Midiendo la pegada…", "Dando ritmo a la máquina…" },
                      { "Tightening the kick drum head…", "Tuning the snare with the key…", "Sweeping the hi-hat…", "Counting to four…",
                        "Polishing the transient click…", "Adjusting the pitch drop…", "Checking it hits you in the chest…", "Preparing the groove…",
                        "Hitting with metronome precision…", "Trimming needless tails…", "Measuring the punch…", "Giving the machine some rhythm…" } } },
    { "Ambient", { { "Bajando las luces…", "Dejando que el sonido flote…", "Abriendo el horizonte…", "Respirando con la reverb…",
                     "Haciendo que el tiempo se detenga…", "Mezclando niebla y brillo…", "Escuchando el silencio entre notas…", "Expandiendo el espacio interior…",
                     "Haciendo que evolucione como el mar…", "Encendiendo estrellas lejanas…", "Meditando con el drone…", "Construyendo un paisaje sonoro…" },
                   { "Dimming the lights…", "Letting the sound float…", "Opening the horizon…", "Breathing with the reverb…",
                     "Making time stand still…", "Blending fog and sparkle…", "Listening to the silence between notes…", "Expanding inner space…",
                     "Letting it evolve like the sea…", "Lighting distant stars…", "Meditating with the drone…", "Building a soundscape…" } } },
};

const std::map<juce::String, std::pair<List, List>> finals {
    { "Bass", { { "¡Listo! Esto mueve el suelo. 🔊", "¡Listo! Grave servido." }, { "Done! This one moves the floor. 🔊", "Done! Low end served." } } },
    { "Lead", { { "¡Listo! Sube al escenario. 🎸", "¡Listo! Un lead con carácter." }, { "Done! Take the stage. 🎸", "Done! A lead with character." } } },
    { "Pad", { { "¡Listo! Cierra los ojos y escucha. ☁", "¡Listo! Un pad para soñar." }, { "Done! Close your eyes and listen. ☁", "Done! A pad to dream on." } } },
    { "Pluck", { { "¡Listo! Esto suena a algo. 🎹", "¡Listo! Pulsa y brilla." }, { "Done! This sounds like something. 🎹", "Done! Pluck and shine." } } },
    { "Keys", { { "¡Listo! A tocar unos acordes. 🎹", "¡Listo! Las teclas están calientes." }, { "Done! Time to play some chords. 🎹", "Done! The keys are warm." } } },
    { "Arp", { { "¡Listo! Mantén un acorde y disfruta. 🔁", "¡Listo! El patrón ya baila." }, { "Done! Hold a chord and enjoy. 🔁", "Done! The pattern is dancing." } } },
    { "Strings", { { "¡Listo! Que suene la orquesta. 🎻", "¡Listo! Cuerdas en su sitio." }, { "Done! Let the orchestra play. 🎻", "Done! Strings in place." } } },
    { "Brass", { { "¡Listo! Fanfarria preparada. 🎺", "¡Listo! Metales relucientes." }, { "Done! Fanfare ready. 🎺", "Done! Shiny brass." } } },
    { "FX", { { "¡Listo! Algo raro y maravilloso. ✨", "¡Listo! Efecto especial cargado." }, { "Done! Something weird and wonderful. ✨", "Done! Special effect loaded." } } },
    { "Drums/Perc", { { "¡Listo! A darle ritmo. 🥁", "¡Listo! Golpe certero." }, { "Done! Let's make a beat. 🥁", "Done! Clean hit." } } },
    { "Ambient", { { "¡Listo! Respira. 🌌", "¡Listo! Un viaje tranquilo." }, { "Done! Breathe. 🌌", "Done! A calm journey." } } },
};
} // namespace

juce::StringArray aiMessages (const juce::String& category, bool english, Rng& rng, int count, juce::String& lastShown)
{
    std::vector<const char*> pool (english ? genericEn.begin() : genericEs.begin(), english ? genericEn.end() : genericEs.end());
    if (auto it = byCategory.find (category); it != byCategory.end())
    {
        const auto& l = english ? it->second.second : it->second.first;
        pool.insert (pool.end(), l.begin(), l.end());
        pool.insert (pool.end(), l.begin(), l.end()); // weight category lines double
    }
    juce::StringArray out;
    int guard = 0;
    while (out.size() < count && guard++ < 200)
    {
        const auto s = juce::String::fromUTF8 (pool[(size_t) (rng.next() % (uint32_t) pool.size())]);
        if (s == lastShown || out.contains (s)) continue; // never repeat back-to-back
        out.add (s);
        lastShown = s;
    }
    if (auto it = finals.find (category); it != finals.end())
    {
        const auto& f = english ? it->second.second : it->second.first;
        out.add (juce::String::fromUTF8 (f[(size_t) (rng.next() % (uint32_t) f.size())]));
    }
    else out.add (english ? "Done!" : juce::String::fromUTF8 ("¡Listo!"));
    return out;
}

int numMessagesFor (const juce::String& category, bool english)
{
    int n = (int) (english ? genericEn.size() : genericEs.size());
    if (auto it = byCategory.find (category); it != byCategory.end()) n += (int) (english ? it->second.second.size() : it->second.first.size());
    return n;
}
}
