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

int carrosNaPonte[2] = {0, 0};
int violacoes = 0;

//protege a verificção dos contadores
HANDLE mutexVerificador;

HANDLE semaforoPonte;

HANDLE semaforoCapacidade;

HANDLE mutexSentido[2];
int carrosSentido[2] = {0, 0};

HANDLE mutexFila;
int fila[MAX_CARROS];
int inicioFila = 0;
int fimFila = 0;

HANDLE podeEntrar[MAX_CARROS];

//registo de entradas e saidas e verifica as regrasa

void verificar(Carro *carro, int aEntrar)
{
    WaitForSingleObject(mutexVerificador, INFINITE);

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

        printf("Violacao! Norte: %d | Sul: %d\n",
               carrosNaPonte[NORTE], carrosNaPonte[SUL]);
    }

    ReleaseMutex(mutexVerificador);
}

void ponte_entrar(Carro *carro)
{
    int sentido = carro -> sentido;
    int indice = carro -> id - 1;
    int seguinte;

    //Pedido na fila
    WaitForSingleObject(mutexFila, INFINITE);
    
    fila [fimFila] = indice;

    //fila vazia, aut carro

    if(inicioFila == fimFila){
        ReleaseSemaphore(podeEntrar[indice], 1, NULL);
    }

    fimFila++;

    ReleaseMutex(mutexFila);

    //esperar pela vez
    WaitForSingleObject(podeEntrar[indice], INFINITE);

    WaitForSingleObject(mutexSentido[sentido], INFINITE);

    carrosSentido[sentido]++;

    if(carrosSentido[sentido] == 1){
        WaitForSingleObject(semaforoPonte, INFINITE);
    }

    ReleaseMutex(mutexSentido[sentido]);

    WaitForSingleObject(semaforoCapacidade, INFINITE);

    verificar(carro, 1);

    //avança a fila depois do carro conseguir entrar
    WaitForSingleObject(mutexFila, INFINITE);

    inicioFila++;

    if (inicioFila < fimFila){
        seguinte = fila[inicioFila];

        ReleaseSemaphore(podeEntrar[seguinte], 1, NULL);
    }

    ReleaseMutex(mutexFila);
}

void ponte_sair(Carro *carro)
{
    int sentido = carro->sentido;

    verificar(carro, 0);

    ReleaseSemaphore(semaforoCapacidade, 1, NULL);

    WaitForSingleObject(mutexSentido[sentido], INFINITE);

    carrosSentido[sentido]--;

    // Apenas o ultimo carro liberta a ponte
    if (carrosSentido[sentido] == 0) {
        ReleaseSemaphore(semaforoPonte, 1, NULL);
    }

    // Todos libertam o mutex do contador
    ReleaseMutex(mutexSentido[sentido]);
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
        if (carros[i].sentido == sentido) {
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

    //mutexz do verificador
    mutexVerificador = CreateMutex(NULL, FALSE, NULL);

    if (mutexVerificador == NULL) return 1;

    semaforoPonte = CreateSemaphore(NULL, 1, 1, NULL);

    if (semaforoPonte == NULL) return 1;

    semaforoCapacidade = CreateSemaphore(NULL, MAX_PONTE, MAX_PONTE, NULL);

    if (semaforoCapacidade == NULL) return 1;

    // Cria os mutexes dos dois sentidos
    for (i = 0; i < 2; i++) {
        mutexSentido[i] = CreateMutex(NULL, FALSE, NULL);

        if (mutexSentido[i] == NULL) return 1;
    }

    // Cria o mutex da fila
    mutexFila = CreateMutex(NULL, FALSE, NULL);

    if (mutexFila == NULL) return 1;

    // Cria o semaforo individual de cada carro
    for (i = 0; i < totalCarros; i++) {
        podeEntrar[i] = CreateSemaphore(NULL, 0, 1, NULL);

        if (podeEntrar[i] == NULL) return 1;
    }

    inicioSimulacao = GetTickCount();

    for (i = 0; i < totalCarros; i++) {
        threads[i] = CreateThread(
            NULL, 0, funcaoCarro, &carros[i], 0, NULL
        );

        if (threads[i] == NULL) return 1;
    }

    for (i = 0; i < totalCarros; i++) {
        WaitForSingleObject(threads[i], INFINITE);
    }

    duracaoSimulacao = GetTickCount() - inicioSimulacao;

    debito = 0;

    if(duracaoSimulacao > 0){
        debito = totalCarros * 1000.0 / duracaoSimulacao;
    }

    // Fecha os handles das threads ja terminadas
    for (i = 0; i < totalCarros; i++) {
        CloseHandle(threads[i]);
    }

    // Fecha os semaforos depois de todas as threads terminarem
    CloseHandle(semaforoPonte);

    CloseHandle(semaforoCapacidade);

    for(i = 0; i < 2; i++){
        CloseHandle(mutexSentido[i]);
    }

    //fecha o semaforo indv de cada carro
    for(i = 0; i < totalCarros; i++){
        CloseHandle(podeEntrar[i]);
    }

    //fechar mtx
    CloseHandle(mutexFila);

        CloseHandle(mutexVerificador);

    mostrarEspera(NORTE, totalCarros);
    mostrarEspera(SUL, totalCarros);
    printf("Debito: %.2f carros por segundo \n", debito);
    printf("Total de violacoes: %d\n", violacoes);

    return 0;
}
