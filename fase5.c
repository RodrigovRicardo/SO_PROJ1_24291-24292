#include <stdio.h>
#include <stdlib.h>
#include <windows.h>

#define MAX_PONTE 3
#define MAX_CARROS 1000

typedef enum {
    NORTE = 0,
    SUL = 1
}Sentido;

typedef struct {
    int id;
    Sentido sentido;
    DWORD chegada;
    DWORD entrada;
    DWORD saida;
} Carro;

//tempos de simulação
DWORD tempoPassagem;
DWORD inicioSimulacao;

DWORD atrasos[MAX_CARROS];

Carro carros [MAX_CARROS];
HANDLE threads[MAX_CARROS];

volatile LONG carrosNaPonte[2] = {0, 0};
volatile LONG violacoes = 0;

//protege a verificção dos contadores
HANDLE mutexVerificador;

HANDLE semaforoCapacidade;

HANDLE mutexFila;
int fila[MAX_CARROS];
int inicioFila = 0;
int fimFila = 0;

HANDLE podeEntrar[MAX_CARROS];

int indiceAmbulancia;
int ambulanciaEspera = 0;
int carrosAutorizados[2] = {0, 0};
int ambulanciaAutorizada = 0;

//registo de entradas e saidas e verifica as regrasa

void verificar(Carro *carro, int aEntrar)
{
    if (WaitForSingleObject(mutexVerificador, INFINITE) != WAIT_OBJECT_0) {
        printf("Erro ao adquirir o mutex do verificador.\n");
        exit(1);
    }

    if (aEntrar) {
        carrosNaPonte[carro->sentido]++;
        carro->entrada = GetTickCount() - inicioSimulacao;
    } else {
        carrosNaPonte[carro->sentido]--;
        carro->saida = GetTickCount() - inicioSimulacao;
    }

    // Verifica sentidos opostos ou capacidade ultrapassada
    if ((carrosNaPonte[NORTE] > 0 && carrosNaPonte[SUL] > 0) ||
        carrosNaPonte[NORTE] + carrosNaPonte[SUL] > MAX_PONTE) {

        violacoes++;

        printf("Violacao! Norte: %ld | Sul: %ld\n",
               carrosNaPonte[NORTE], carrosNaPonte[SUL]);
    }

    if (!ReleaseMutex(mutexVerificador)) {
        printf("Erro ao libertar o mutex do verificador.\n");
        exit(1);
    }
}

void autorizarEntrada(void){
    int seguinte;
    int sentido;

    if(ambulanciaEspera){
        if (ambulanciaAutorizada){
            return;
        }

        seguinte = indiceAmbulancia;
    }else{
        if (inicioFila == fimFila){
            return;
        }

        seguinte = fila[inicioFila];
    }

    sentido = carros[seguinte].sentido;

    if(carrosAutorizados[1 - sentido] > 0){
        return;
    }

    if (carrosAutorizados[NORTE] + carrosAutorizados[SUL] >= MAX_PONTE){
        return;
    }

    carrosAutorizados[sentido]++;

    if(seguinte == indiceAmbulancia){
        ambulanciaAutorizada = 1;
    }else{
        inicioFila++;
    }

    if(!ReleaseSemaphore(podeEntrar[seguinte], 1, NULL)){
        printf("Erro ao autorizar a entrada.\n");
        exit(1);
    }
}

void ponte_entrar(Carro *carro)
{
    int indice = carro -> id - 1;

    //Pedido na fila
    if (WaitForSingleObject(mutexFila, INFINITE) != WAIT_OBJECT_0){
        printf("Erro ao adquirir o mutex da fila. \n");
        exit(1);
    }
    
    if(indice == indiceAmbulancia){
        ambulanciaEspera = 1;
    }else{
        fila[fimFila] = indice;
        fimFila++;
    }

    autorizarEntrada();

    if(!ReleaseMutex(mutexFila)){
        printf("Erro ao libertar o mutex da fila. \n");
        exit(1);
    }

    //esperar pela vez
    if(WaitForSingleObject(podeEntrar[indice], INFINITE) != WAIT_OBJECT_0){
        printf("Erro ao esperar pela vez do carro. \n");
        exit(1);
    }

    if (WaitForSingleObject(semaforoCapacidade, INFINITE) != WAIT_OBJECT_0) {
        printf("Erro ao adquirir um lugar na ponte.\n");
        exit(1);
    }

    verificar(carro, 1);

    //avança a fila depois do carro conseguir entrar
    if (WaitForSingleObject(mutexFila, INFINITE) != WAIT_OBJECT_0) {
        printf("Erro ao adquirir o mutex da fila.\n");
        exit(1);
    }


    if (indice == indiceAmbulancia){
        ambulanciaEspera = 0;

    }

    autorizarEntrada();

    if(!ReleaseMutex(mutexFila)){
        printf("Erro ao libertar o mutex da fila.\n");
        exit(1);
    }
}

void ponte_sair(Carro *carro)
{
    int sentido = carro->sentido;
    int indice = carro->id - 1;

    verificar(carro, 0);

    if (!ReleaseSemaphore(semaforoCapacidade, 1, NULL)) {
        printf("Erro ao libertar um lugar na ponte.\n");
        exit(1);
    }

    if (WaitForSingleObject(mutexFila, INFINITE) != WAIT_OBJECT_0) {
        printf("Erro ao adquirir o mutex da fila.\n");
        exit(1);
    }

    carrosAutorizados[sentido]--;

    if (indice == indiceAmbulancia){
        ambulanciaAutorizada = 0;
    }

    autorizarEntrada();

    // Todos libertam o mutex do contador
    if (!ReleaseMutex(mutexFila)) {
        printf("Erro ao libertar o mutex da fila.\n");
        exit(1);
    }
}

DWORD WINAPI funcaoCarro(LPVOID parametro){
    Carro *carro = (Carro *)parametro;

    DWORD atraso = atrasos[carro -> id - 1];
    DWORD tempoDecorrido = GetTickCount() - inicioSimulacao;

    if(atraso > tempoDecorrido){
        Sleep(atraso - tempoDecorrido);
    }

    carro -> chegada = GetTickCount() - inicioSimulacao;

    ponte_entrar(carro);
    Sleep(tempoPassagem);
    ponte_sair(carro);

    return 0;
}

// Calcula as esperas dos carros de um sentido
void mostrarEspera(Sentido sentido, int totalCarros)
{
    int posicaoP95;
    int i;
    int j;
    int quantidade = 0;
    DWORD espera;
    DWORD esperaMaxima = 0;
    double soma = 0;

    DWORD auxiliar;
    DWORD esperas[MAX_CARROS];

    for (i = 0; i < totalCarros; i++) {
        if (i != indiceAmbulancia && carros[i].sentido == sentido) {
            espera = carros[i].entrada - carros[i].chegada;

            esperas[quantidade] = espera;
            soma += espera;
            quantidade++;

            if (espera > esperaMaxima) {
                esperaMaxima = espera;
            }
        }
    }

    if (sentido == NORTE) {
        printf("\nEsperas do NORTE:\n");
    } else {
        printf("\nEsperas do SUL:\n");
    }

    if (quantidade == 0) {
        printf("Sem carros deste sentido.\n");
        return;
    }

    posicaoP95 = (95 * quantidade + 99) / 100;

    for(i = 0; i < quantidade; i++) {
        for(j = i+1; j < quantidade; j++){
            if(esperas[j] < esperas [i]) {
                auxiliar = esperas[i];
                esperas[i] = esperas[j];
                esperas[j] = auxiliar;
            }
        }
    }

    printf("Percentil 95: %lu ms\n", esperas[posicaoP95 - 1]);
    printf("Espera media: %.2f ms\n", soma / quantidade);
    printf("Espera maxima: %lu ms\n", esperaMaxima);
}

int main(int argc, char *argv[])
{
    int nNorte;
    int nSul;
    int intervaloChegada;
    int passagem;

    unsigned int semente;

    int i;
    int totalCarros;
    DWORD atrasoNorte = 0;
    DWORD atrasoSul = 0;
    DWORD duracaoSimulacao;
    double debito;

    if(argc != 5 && argc != 6){
        printf("Uso: ponte.exe <n_norte> <n_sul> <chegada_ms> <travessia_ms> [semente]");
        return 1;
    }

    nNorte = atoi(argv[1]);
    nSul = atoi(argv[2]);
    intervaloChegada = atoi(argv[3]);
    passagem = atoi(argv[4]);

//verificar qntd de carros de cada lado
    if (nNorte < 0 || nSul < 0 || nNorte > MAX_CARROS || nSul > MAX_CARROS) {
        printf("Quantidade de carros invalida. \n");
        
        return 1;
    }

    if (nNorte + nSul == 0 || nNorte + nSul > MAX_CARROS) {
        printf("O total deve estar entre 1 e %d carros. \n", MAX_CARROS);

        return 1;
    }

    if(intervaloChegada < 0 || passagem < 0) {
        printf("Os tempos nao podem ser negativos. \n");

        return 1;
    }

    tempoPassagem = passagem;

    if(argc == 6) {
        semente = atoi(argv[5]);
    } else {   
        semente = GetTickCount();
    }
    srand(semente);

    totalCarros = nNorte + nSul;

    for (i = 0; i < totalCarros; i++) {
        carros[i].id = i + 1;

        if (i < nNorte) {
            carros[i].sentido = NORTE;

            atrasoNorte += rand() % (intervaloChegada + 1);
            atrasos[i] = atrasoNorte;
        } else {
            carros[i].sentido = SUL;

            atrasoSul += rand() % (intervaloChegada + 1);
            atrasos[i] = atrasoSul;
        }
    }

    if(totalCarros >= MAX_CARROS){
        printf("Falta uma posicao para a ambulancia.\n");
        return 1;
    }

    indiceAmbulancia = totalCarros;

    carros[indiceAmbulancia].id = indiceAmbulancia +1;
    carros[indiceAmbulancia].sentido = NORTE;
    atrasos[indiceAmbulancia] = intervaloChegada;

    totalCarros++;

    //mutex do verificador
    mutexVerificador = CreateMutex(NULL, FALSE, NULL);

    if (mutexVerificador == NULL) {
        printf("Erro ao criar o mutex do verificador.\n");
        return 1;
    }

    semaforoCapacidade = CreateSemaphore(NULL, MAX_PONTE, MAX_PONTE, NULL);

    if (semaforoCapacidade == NULL) {
        printf("Erro ao criar o semaforo da capacidade. \n");
        return 1;
    }

   
    // Cria o mutex da fila
    mutexFila = CreateMutex(NULL, FALSE, NULL);

    if (mutexFila == NULL) {
        printf("Erro ao criar o mutex da fila.\n");
        return 1;
    }

    // Cria o semaforo individual de cada carro
    for (i = 0; i < totalCarros; i++) {
        podeEntrar[i] = CreateSemaphore(NULL, 0, 1, NULL);

        if (podeEntrar[i] == NULL) {
            printf("Erro ao criar o semaforo do carro %d.\n", carros[i].id);
            return 1;
        }
    }

    inicioSimulacao = GetTickCount();

    
    for (i = 0; i < totalCarros; i++) {
        threads[i] = CreateThread(
            NULL, 0, funcaoCarro, &carros[i], 0, NULL
        );

        if (threads[i] == NULL) {
            printf("Erro ao criar a thread do carro %d.\n", carros[i].id);
            return 1;
        }
    }

    for (i = 0; i < totalCarros; i++) {
        if (WaitForSingleObject(threads[i], INFINITE) != WAIT_OBJECT_0) {
            printf("Erro ao esperar pela thread do carro %d.\n", carros[i].id);
            return 1;
        }
    }

    duracaoSimulacao = GetTickCount() - inicioSimulacao;

    debito = 0;

    if(duracaoSimulacao > 0){
        debito = totalCarros * 1000.0 / duracaoSimulacao;
    }

    // Fecha os handles das threads ja terminadas
    for (i = 0; i < totalCarros; i++) {
        if (!CloseHandle(threads[i])) {
            printf("Erro ao fechar o handle da thread.\n");
            return 1;
        }
    }

    if (!CloseHandle(semaforoCapacidade)) {
        printf("Erro ao fechar o semaforo da capacidade.\n");
        return 1;
    }

    //fecha o semaforo indv de cada carro
    for(i = 0; i < totalCarros; i++){
        if(!CloseHandle(podeEntrar[i])){
            printf("Erro ao fechar o semaforo do carro %d.\n", carros[i].id);
            return 1;
        }
    }

    //fechar mtx
    if(!CloseHandle(mutexFila)){
        printf("Erro ao fechar o mutex da fila. \n");
        return 1;
    }

        if (!CloseHandle(mutexVerificador)) {
        printf("Erro ao fechar o mutex do verificador.\n");
        return 1;
    }

    printf("\nEspera da ambulancia: %lu ms\n", carros[indiceAmbulancia].entrada - carros[indiceAmbulancia].chegada);
    printf("\nEsperas dos carros normais:\n");

    mostrarEspera(NORTE, totalCarros);
    mostrarEspera(SUL, totalCarros);
    printf("Debito: %.2f carros por segundo \n", debito);
    printf("Total de violacoes: %ld\n", violacoes);

    return 0;
}