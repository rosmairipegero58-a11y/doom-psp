# DOOM PSP

Port nativo a C de tu juego HTML (mismos 27+200 niveles, enemigos, armas, musica).

## Controles
Stick/cruceta: mover y girar | L/R: paso lateral | X o CUADRADO: disparar |
CIRCULO: abrir puertas/salida | TRIANGULO o SELECT: cambiar arma | START: pausa

## Obtener el .iso (desde el movil, con GitHub)
1. Crea un repositorio nuevo en github.com (boton + > New repository).
2. *Add file > Upload files*: sube todos los archivos de esta carpeta que estan
   en el nivel principal (Makefile, game.c, audio.c, psp_main.c, game.h, audio.h,
   font.h, ICON0.PNG, make_iso.py). Pulsa *Commit changes*.
3. *Add file > Create new file*. En el nombre escribe exactamente
   `.github/workflows/build.yml` y pega el contenido de `build.yml.txt`. Commit.
4. Pestana *Actions*: se ejecuta solo (o elige "Compilar DOOM PSP" > Run workflow).
   Tarda unos minutos. Al terminar entra y descarga el artefacto `DOOM-PSP`.
5. Dentro del zip estan `DOOM_PSP.iso` y `EBOOT.PBP`. Pega el .iso junto a tus juegos.

El progreso se guarda en la tarjeta de memoria de PPSSPP (PSP/SAVEDATA/DOOMPSP).
Si el .iso no abriera, `EBOOT.PBP` funciona con *Cargar* en PPSSPP.
