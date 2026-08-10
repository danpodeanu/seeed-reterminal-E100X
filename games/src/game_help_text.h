#pragma once

#include <stddef.h>
#include <stdint.h>

#include "game_localization.h"

namespace game_help {

enum class Topic : uint8_t {
  LightsOut,
  Game2048,
  PipeConnect,
  Minesweeper,
  Nonogram,
  Reversi,
  DotsAndBoxes,
  Sokoban,
  PegSolitaire,
  Slitherlink,
  Sudoku,
  Crossword,
  Klondike,
  MahjongSolitaire,
  EpubReader,
  Count,
};

constexpr size_t kTopicCount = static_cast<size_t>(Topic::Count);
constexpr size_t kColumnsPerLine = 34;
constexpr size_t kMaximumLines = 21;

using game_localization::Utf8Char;

inline constexpr const Utf8Char*
    kInstructions[kTopicCount][game_localization::kLanguageCount] = {
        {u8"Turn all 25 lights off. Tapping a square toggles it and its "
         "horizontal and vertical neighbours. NEW creates another solvable "
         "puzzle. RESET restores the current puzzle.",
         u8"Apaga las 25 luces. Tocar una casilla cambia esa luz y sus vecinas "
         "horizontal y vertical. NUEVO crea otro puzle resoluble. REINICIAR "
         "restaura el puzle actual.",
         u8"Éteignez les 25 lumières. Toucher une case change cette lumière et "
         "ses voisines horizontales et verticales. NOUVEAU crée une autre "
         "grille. RÉINITIALISER restaure la grille actuelle.",
         u8"Schalte alle 25 Lichter aus. Ein Feld ändert sich zusammen mit "
         "seinen waagerechten und senkrechten Nachbarn. NEU erzeugt ein neues "
         "lösbares Rätsel. ZURÜCKSETZEN stellt das aktuelle wieder her.",
         u8"关闭全部 25 盏灯。点击一个方格会切换它以及上下左右相邻方格。新游戏会生成另一道"
         "可解题目，重置会恢复当前题目。"},
        {u8"Swipe the board up, down, left, or right. Equal tiles merge and "
         "add to your score; each valid move adds a new 2 or 4. Reach 2048. "
         "NEW starts over but keeps the best score.",
         u8"Desliza el tablero arriba, abajo, izquierda o derecha. Las fichas "
         "iguales se unen y suman puntos; cada movimiento válido añade un 2 o "
         "un 4. Llega a 2048. NUEVO reinicia y conserva el récord.",
         u8"Balayez la grille vers le haut, le bas, la gauche ou la droite. Les "
         "tuiles égales fusionnent et marquent des points; chaque coup valide "
         "ajoute un 2 ou un 4. Atteignez 2048. NOUVEAU conserve le record.",
         u8"Wische nach oben, unten, links oder rechts. Gleiche Kacheln "
         "verschmelzen und erhöhen die Punktzahl; nach jedem gültigen Zug "
         "erscheint eine 2 oder 4. Erreiche 2048. NEU behält den Rekord.",
         u8"向上、下、左或右滑动棋盘。相同数字会合并并得分，每次有效移动会新增 2 或 4。目标是"
         "合成 2048。新游戏会保留最高分。"},
        {u8"Connect every pipe into one network. Tap a tile to rotate it "
         "clockwise. NEW creates another network. RESET restores the current "
         "scramble.",
         u8"Conecta todas las tuberías en una sola red. Toca una pieza para "
         "girarla a la derecha. NUEVO crea otra red. REINICIAR restaura la "
         "mezcla actual.",
         u8"Reliez tous les tuyaux en un seul réseau. Touchez une tuile pour la "
         "tourner dans le sens horaire. NOUVEAU crée un autre réseau. "
         "RÉINITIALISER restaure le mélange actuel.",
         u8"Verbinde alle Rohre zu einem Netz. Tippe eine Kachel an, um sie im "
         "Uhrzeigersinn zu drehen. NEU erzeugt ein anderes Netz. ZURÜCKSETZEN "
         "stellt die aktuelle Mischung wieder her.",
         u8"把所有管道连接成一个网络。点击方块可顺时针旋转。新游戏会生成另一张管网，重置会恢复"
         "当前乱序。"},
        {u8"Clear the field without opening a mine. Tap to reveal a tile. Hold "
         "a covered tile for 650 ms to add or remove a flag. Tapping a revealed "
         "number opens its other neighbours when enough adjacent flags exist.",
         u8"Despeja el campo sin abrir una mina. Toca para revelar una casilla. "
         "Mantén una casilla cubierta 650 ms para poner o quitar una bandera. "
         "Toca un número revelado para abrir sus vecinas cuando haya suficientes "
         "banderas.",
         u8"Videz le terrain sans ouvrir de mine. Touchez pour révéler une case. "
         "Maintenez une case couverte 650 ms pour poser ou retirer un drapeau. "
         "Touchez un nombre révélé quand assez de drapeaux l'entourent.",
         u8"Räume das Feld, ohne eine Mine zu öffnen. Tippen deckt ein Feld auf. "
         "Halte ein verdecktes Feld 650 ms, um eine Flagge zu setzen oder zu "
         "entfernen. Tippe eine Zahl an, wenn genügend Nachbarflaggen stehen.",
         u8"在不触雷的情况下清空棋盘。点击可翻开方格，长按未翻开的方格 650 毫秒可添加或移除旗帜。"
         "相邻旗帜数量足够时，点击已显示的数字可翻开其余相邻方格。"},
        {u8"Use the row and column clues to reveal the hidden picture. Each "
         "number is a run of filled cells; separate runs have at least one "
         "blank between them. Tap a cell to cycle blank, filled, crossed.",
         u8"Usa las pistas de filas y columnas para descubrir la imagen. Cada "
         "número indica un grupo seguido de casillas llenas; los grupos se "
         "separan por al menos una vacía. Toca para alternar vacío, lleno y X.",
         u8"Utilisez les indices des lignes et colonnes pour révéler l'image. "
         "Chaque nombre indique une suite de cases pleines; deux suites sont "
         "séparées par au moins une case vide. Touchez pour alterner vide, plein "
         "et croix.",
         u8"Nutze die Zeilen- und Spaltenhinweise für das versteckte Bild. Jede "
         "Zahl steht für eine Folge gefüllter Felder; Folgen haben mindestens "
         "ein Leerfeld Abstand. Tippen wechselt leer, gefüllt und Kreuz.",
         u8"根据行列提示找出隐藏图案。每个数字表示一段连续填充方格，多段之间至少有一个空格。点击"
         "方格可在空白、填充和叉号之间切换。"},
        {u8"Trap opposing discs between the disc you place and another of your "
         "colour; trapped lines flip. Dots mark legal moves. The most discs "
         "wins when neither player can move. Choose 1 PLAYER or 2 PLAYERS.",
         u8"Encierra discos rivales entre el que colocas y otro de tu color; la "
         "línea encerrada cambia de color. Los puntos marcan jugadas válidas. "
         "Gana quien tenga más discos. Elige 1 JUGADOR o 2 JUGADORES.",
         u8"Encadrez les pions adverses entre le pion posé et un autre de votre "
         "couleur; la ligne capturée se retourne. Les points indiquent les coups "
         "valides. Le plus grand nombre de pions gagne. Choisissez 1 ou 2 joueurs.",
         u8"Schließe gegnerische Steine zwischen dem gesetzten und einem eigenen "
         "Stein ein; die Reihe wird umgedreht. Punkte zeigen gültige Züge. Wer "
         "am Ende die meisten Steine hat, gewinnt. Wähle 1 oder 2 Spieler.",
         u8"落子时夹住对方棋子即可将其翻转，圆点表示合法位置。双方都无法落子时，棋子多者获胜。"
         "可选择单人或双人模式。"},
        {u8"Take turns drawing one edge between adjacent dots. Completing a box "
         "claims it and gives the same player another turn. The player with the "
         "most boxes after every edge is drawn wins.",
         u8"Por turnos, dibuja una línea entre puntos vecinos. Completar una "
         "caja la reclama y concede otro turno. Gana quien tenga más cajas "
         "cuando se hayan dibujado todas las líneas.",
         u8"À tour de rôle, tracez un côté entre deux points voisins. Fermer une "
         "case la capture et donne un nouveau tour. Le joueur ayant le plus de "
         "cases lorsque tous les côtés sont tracés gagne.",
         u8"Zeichnet abwechselnd eine Kante zwischen benachbarten Punkten. Wer "
         "ein Kästchen schließt, erhält es und ist noch einmal am Zug. Am Ende "
         "gewinnt die Person mit den meisten Kästchen.",
         u8"轮流连接相邻圆点。完成一个方框即可占有它并继续行动。所有边都画完后，占有方框最多的"
         "玩家获胜。"},
        {u8"Push every crate onto a target. Crates can be pushed but never "
         "pulled, so avoid trapping them against walls. Tap the board in the "
         "direction to move. RESTART retries the level; NEXT opens the next "
         "unfinished level after a win.",
         u8"Empuja cada caja hasta un objetivo. Las cajas se empujan pero no se "
         "pueden tirar, así que no las bloquees contra paredes. Toca el tablero "
         "en la dirección del movimiento. REINICIAR repite el nivel y SIGUIENTE "
         "abre el próximo nivel pendiente.",
         u8"Poussez chaque caisse sur une cible. Une caisse se pousse mais ne se "
         "tire jamais; évitez de la bloquer contre un mur. Touchez la grille "
         "dans la direction voulue. RECOMMENCER reprend le niveau et SUIVANT "
         "ouvre le prochain niveau non terminé.",
         u8"Schiebe jede Kiste auf ein Ziel. Kisten lassen sich nur schieben, "
         "nicht ziehen; vermeide Sackgassen an Wänden. Tippe in Bewegungsrichtung "
         "auf das Feld. NEUSTART wiederholt den Level, WEITER öffnet den nächsten "
         "ungelösten Level.",
         u8"把所有箱子推到目标点。箱子只能推不能拉，请避免把箱子卡在墙边。点击棋盘中想移动的方向。"
         "重新开始可重试本关，完成后点击下一关进入下一个未完成关卡。"},
        {u8"Remove pegs by jumping one peg over an adjacent peg into an empty "
         "hole. The jumped peg disappears. Select a peg, then tap a legal hole "
         "two spaces away. Try to finish with one peg in the centre.",
         u8"Elimina fichas saltando una sobre otra vecina hasta un hueco vacío; "
         "la ficha saltada desaparece. Selecciona una ficha y toca un hueco "
         "válido a dos posiciones. Intenta acabar con una ficha en el centro.",
         u8"Retirez les pions en sautant par-dessus un pion voisin vers un trou "
         "vide; le pion sauté disparaît. Sélectionnez un pion puis une arrivée "
         "valide à deux cases. Essayez de finir avec un pion au centre.",
         u8"Entferne Steine, indem einer über einen Nachbarn in ein leeres Loch "
         "springt; der übersprungene Stein verschwindet. Wähle einen Stein und "
         "dann ein gültiges Loch zwei Stellen entfernt. Ziel ist ein Stein in "
         "der Mitte.",
         u8"让棋子跳过相邻棋子进入空位，被跳过的棋子会移除。先选择棋子，再点击相隔两格的合法空位。"
         "目标是最后只在中心留下一个棋子。"},
        {u8"Draw one continuous loop. Each number tells how many of that cell's "
         "four edges belong to the loop. Tap an edge to cycle blank, line, and "
         "cross. The loop cannot branch or cross itself.",
         u8"Dibuja un único bucle continuo. Cada número indica cuántos de los "
         "cuatro lados de esa celda pertenecen al bucle. Toca un lado para "
         "alternar vacío, línea y X. El bucle no puede ramificarse ni cruzarse.",
         u8"Tracez une seule boucle continue. Chaque nombre indique combien des "
         "quatre côtés de sa case appartiennent à la boucle. Touchez un côté "
         "pour alterner vide, trait et croix. La boucle ne se divise ni ne se "
         "croise.",
         u8"Zeichne eine einzige geschlossene Schleife. Jede Zahl gibt an, wie "
         "viele der vier Zellkanten zur Schleife gehören. Tippen wechselt leer, "
         "Linie und Kreuz. Die Schleife darf sich weder verzweigen noch kreuzen.",
         u8"画出一条连续闭环。每个数字表示其方格四条边中有几条属于闭环。点击边可在空白、线段和叉号"
         "之间切换。闭环不能分叉或自交。"},
        {u8"Fill every row, column, and thick 3x3 box with digits 1 through 9 "
         "without repeats. Select an empty square, then tap a number. X clears "
         "the selected square. Conflicting digits are rejected.",
         u8"Completa cada fila, columna y bloque grueso de 3x3 con los números "
         "del 1 al 9 sin repetir. Selecciona una casilla vacía y toca un número. "
         "X borra la casilla. Los conflictos se rechazan.",
         u8"Remplissez chaque ligne, colonne et bloc épais de 3x3 avec les "
         "chiffres 1 à 9 sans répétition. Sélectionnez une case vide puis un "
         "chiffre. X efface la case. Les conflits sont refusés.",
         u8"Fülle jede Zeile, Spalte und jeden dicken 3x3-Block ohne "
         "Wiederholung mit 1 bis 9. Wähle ein leeres Feld und dann eine Zahl. X "
         "löscht das Feld. Ungültige Zahlen werden abgelehnt.",
         u8"在每一行、每一列和每个粗线 3×3 宫内填入 1 到 9，且不能重复。先选择空格，再点击数字。"
         "X 可清除所选格，冲突数字会被拒绝。"},
        {u8"Fill the white squares from the Across and Down clues. Tap a square "
         "to open the keyboard; tap an intersecting square again to change "
         "direction. Use letters, DEL to erase, and OK to close the keyboard.",
         u8"Rellena las casillas blancas con las pistas horizontales y "
         "verticales. Toca una casilla para abrir el teclado; toca de nuevo una "
         "intersección para cambiar de dirección. Usa letras, BORRAR y OK.",
         u8"Remplissez les cases blanches à l'aide des définitions horizontales "
         "et verticales. Touchez une case pour ouvrir le clavier; retouchez une "
         "intersection pour changer de direction. Utilisez les lettres, EFF. et "
         "OK.",
         u8"Fülle die weißen Felder mithilfe der waagerechten und senkrechten "
         "Hinweise. Tippe ein Feld für die Tastatur an; tippe einen Schnittpunkt "
         "erneut an, um die Richtung zu wechseln. Nutze Buchstaben, LÖSCH. und OK.",
         u8"根据横向和纵向提示填写白色方格。点击方格可打开键盘，再次点击交叉方格可切换方向。使用字母"
         "输入，删除键可擦除，OK 可关闭键盘。"},
        {u8"Build each suit from Ace to King on the four foundations. Tableau "
         "runs descend in alternating colours; only a King enters an empty "
         "column. Tap the stock to draw. Select cards, then tap a destination. "
         "Double-tap an available card to send it to its foundation.",
         u8"Completa cada palo del As al Rey en las cuatro bases. Las columnas "
         "bajan alternando colores; solo un Rey entra en una columna vacía. Toca "
         "el mazo para robar. Selecciona cartas y luego el destino. Un doble toque "
         "envía una carta disponible a su base.",
         u8"Montez chaque couleur de l'As au Roi sur les quatre fondations. Les "
         "suites du tableau descendent en alternant les couleurs; seul un Roi "
         "entre dans une colonne vide. Touchez la pioche, sélectionnez des cartes "
         "puis leur destination. Un double toucher envoie vers la fondation.",
         u8"Baue jede Farbe vom Ass bis zum König auf den vier Ablagen auf. "
         "Tableaufolgen laufen abwärts mit wechselnden Farben; nur ein König darf "
         "in eine leere Spalte. Tippe den Stapel zum Ziehen an. Wähle Karten und "
         "dann das Ziel. Doppeltippen legt eine passende Karte automatisch ab.",
         u8"在四个基础牌堆中按花色从 A 排到 K。桌面牌按红黑交替降序排列，空列只能放 K。点击牌库"
         "摸牌，先选择牌再点击目标位置。双击可用牌会自动移到对应基础牌堆。"},
        {u8"Remove matching pairs of free tiles. A tile is free when no tile "
         "covers it and at least one horizontal side is open. Tap one free tile, "
         "then a matching free tile. A poor pairing can still leave no moves.",
         u8"Retira parejas iguales de fichas libres. Una ficha está libre si "
         "ninguna la cubre y al menos un lado horizontal está abierto. Toca una "
         "ficha libre y después otra igual. Una mala elección puede dejarte sin "
         "movimientos.",
         u8"Retirez des paires identiques de tuiles libres. Une tuile est libre "
         "si aucune ne la couvre et si au moins un côté horizontal est dégagé. "
         "Touchez une tuile libre puis sa jumelle. Un mauvais choix peut bloquer "
         "la partie.",
         u8"Entferne gleiche Paare freier Steine. Ein Stein ist frei, wenn keiner "
         "darauf liegt und mindestens eine waagerechte Seite offen ist. Wähle "
         "einen freien Stein und dann seinen gleichen Partner. Eine schlechte "
         "Wahl kann alle weiteren Züge blockieren.",
         u8"移除两个相同且可用的牌。没有牌覆盖并且左右至少一侧畅通时，该牌才可用。先点击一张可用牌，"
         "再点击相同的可用牌。选择不当可能导致无牌可消。"},
        {u8"Browse the SD card read-only and tap an EPUB file to open it. UP and "
         "DOWN change browser or reading pages. UP FOLDER returns to the parent "
         "folder. The reading-screen back arrow returns to the browser. Only "
         "DRM-free, reflowable EPUB 2 and EPUB 3 books are supported.",
         u8"Explora la tarjeta SD en modo de solo lectura y toca un EPUB para "
         "abrirlo. ARRIBA y ABAJO cambian de página al explorar o leer. SUBIR "
         "CARPETA vuelve al directorio superior. La flecha de lectura vuelve al "
         "explorador. Solo se admiten EPUB 2/3 fluidos y sin DRM.",
         u8"Parcourez la carte SD en lecture seule et touchez un EPUB pour "
         "l'ouvrir. HAUT et BAS changent de page dans le navigateur ou le livre. "
         "DOSSIER PARENT remonte d'un niveau. La flèche de lecture revient au "
         "navigateur. Seuls les EPUB 2/3 redistribuables sans DRM sont pris en "
         "charge.",
         u8"Durchsuche die SD-Karte schreibgeschützt und tippe eine EPUB-Datei "
         "zum Öffnen an. AUF und AB wechseln Browser- oder Buchseiten. ORDNER "
         "NACH OBEN öffnet den Elternordner. Der Lesepfeil kehrt zum Browser "
         "zurück. Unterstützt werden nur DRM-freie, umfließende EPUB-2/3-Bücher.",
         u8"以只读方式浏览 SD 卡，点击 EPUB 文件即可打开。侧边上、下键可切换浏览页或阅读页。上级"
         "文件夹可返回父目录，阅读界面的返回箭头可回到浏览器。仅支持无 DRM 的可重排 EPUB 2/3"
         " 图书。"},
};

inline const char* text(game_localization::Language language, Topic topic) {
  const size_t languageIndex = static_cast<size_t>(language);
  const size_t topicIndex = static_cast<size_t>(topic);
  return game_localization::utf8Text(
      kInstructions[topicIndex < kTopicCount ? topicIndex : 0]
                   [languageIndex < game_localization::kLanguageCount
                        ? languageIndex
                        : 0]);
}

}  // namespace game_help
