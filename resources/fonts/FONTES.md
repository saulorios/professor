# Fontes Hershey

- `rowmans.jhf` — Roman Simplex: usada pelo comando `escrever`.
- `scripts.jhf` — Script Simplex (cursiva): incluída para uso futuro.
- `hershey.txt` — nota original da distribuição Usenet, com a restrição de uso.
  Ela precisa acompanhar os dados das fontes.

Origem: <https://github.com/kamalmostafa/hershey-fonts>, diretório `hershey-fonts/`,
que por sua vez obteve os dados em <http://emergent.unpythonic.net/software/hershey>.
Só os dados das fontes (`.jhf`) e a nota foram copiados, sem nenhum código daquele
repositório. Os `.jhf` seguem a licença própria das Hershey Fonts descrita em
`hershey.txt`, e não a GPL da biblioteca daquele projeto.

As fontes Hershey foram criadas pelo Dr. A. V. Hershey no U. S. National Bureau of
Standards. O formato dos dados foi criado por James Hurt (Cognition, Inc.).

Os arquivos `.jhf` trazem só o ASCII (32–127). As letras acentuadas do português,
o grau (°) e o ponto de multiplicação (·) são compostos em `src/scene/HersheyFont.cpp`.
