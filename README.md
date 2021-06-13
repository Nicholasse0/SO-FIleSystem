# File System

Il progetto prevede la realizzazione di un file system che si occupa quindi della gestione di file di testo o file di cartella. La gestione di questi avviene tramite una linked list.

Ogni blocco è formato da un header e da uno spazio adibito alle informazioni del file in questione, ma come accennato in precedenza è necessario effettuare una distinzione tra due tipologie di blocchi:
- File di testo, ossia blocchi di dati contenti informazioni casuali. Questi sono formati da un primo blocco (FirstFileBlock), contiene l’header, il FileControlBlock e le informazioni definite da un'array di char (data). I blocchi successivi invece sono formati da un header e un’array di char (data).
Un file di testo è quindi caratterizzato da un FirstFileBlock e da eventuali altri blocchi, i FileBlock.
- File di cartella ossia file contenenti altri file. Questi, così come per i file di testo, sono formati da un primo blocco (FirstDirectoryBlock) che contiene l’header, il FileControlBlock e i blocchi iniziali dei file(testo o cartella) all'interno della cartella. I blocchi successivi invece sono formati da un header e un array di primi blocchi.
Un file di cartella è quindi caratterizzato da un FirstControlBlock e da eventuali altri blocchi, i DirectoryBlock.

### Funzioni
Il progetto prevede l'implementazione di una bitmap utilizzata per la gestione dello spazio di memoria.
Implementazione di un Disk Driver che prevede la creazione del disco che si andrà ad usare e tutte le funzioni che permettono la manipolazione dei blocchi del disco.
In fine l'implementazioe del file system e di tutte le funzioni necessarie alla gestione dei file di testo e cartella(creazione, rimozione e manipolazione).

### Esecuzione
L'esecuzione del programma consente di verificare che tutte le funzioni implementate, sia bitmap che disk driver che file system, funzionino correttamente. Si ha una suddivisione delle relative funzioni implementate in tre sezioni, dove ognuna permette la verifica delle funzioni della rispettiva sezione.