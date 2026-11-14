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

HANDLE mutexPonte;
HANDLE eventoPonteLivre;

int sentidoReservado = -1;

HANDLE semaforoCapacidade;

HANDLE mutexSentido[2];
int carrosSentido[2] = {0, 0};

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
    int sentido = carro->sentido;

    WaitForSingleObject(mutexSentido[sentido], INFINITE);

    /* O primeiro carro do grupo reserva o sentido da ponte. */
    if (carrosSentido[sentido] == 0) {

        WaitForSingleObject(mutexPonte, INFINITE);

        while (sentidoReservado != -1) {

            /* Liberta o mutex antes de esperar pela ponte livre. */
            ReleaseMutex(mutexPonte);

            WaitForSingleObject(eventoPonteLivre, INFINITE);

            /* Volta a proteger o estado antes de o consultar. */
            WaitForSingleObject(mutexPonte, INFINITE);
        }

        sentidoReservado = sentido;

        ResetEvent(eventoPonteLivre);

        ReleaseMutex(mutexPonte);
    }

    carrosSentido[sentido]++;

    ReleaseMutex(mutexSentido[sentido]);

    /* Cada carro precisa de um dos tres lugares. */
    WaitForSingleObject(semaforoCapacidade, INFINITE);

    verificar(carro, 1);
}

void ponte_sair(Carro *carro)
{
    int sentido = carro->sentido;

    verificar(carro, 0);

    /* Devolve um lugar na ponte. */
    ReleaseSemaphore(semaforoCapacidade, 1, NULL);

    WaitForSingleObject(mutexSentido[sentido], INFINITE);

    carrosSentido[sentido]--;

    /* O ultimo carro do grupo liberta a reserva. */
    if (carrosSentido[sentido] == 0) {

        WaitForSingleObject(mutexPonte, INFINITE);

        sentidoReservado = -1;

        /* Avisa os carros que aguardam pela ponte livre. */
        SetEvent(eventoPonteLivre);

        ReleaseMutex(mutexPonte);
    }

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
    int norteRestantes;
    int sulRestantes;

    unsigned int semente;

    int i;
    int totalCarros;
    DWORD atrasoNorte = 0;
    DWORD atrasoSul = 0;
    DWORD duracaoSimulacao;
    int debito;

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
    norteRestantes = nNorte;
    sulRestantes = nSul;

    for (i = 0; i < totalCarros; i++) {
        carros[i].id = i + 1;

        if (rand() % (norteRestantes + sulRestantes) < norteRestantes) {
            carros[i].sentido = NORTE;
            norteRestantes--;

            atrasoNorte += rand() % (intervaloChegada + 1);
            atrasos[i] = atrasoNorte;
        } else {
            carros[i].sentido = SUL;
            sulRestantes--;

            atrasoSul += rand() % (intervaloChegada + 1);
            atrasos[i] = atrasoSul;
        }
    }

    //mutexz do verificador
    mutexVerificador = CreateMutex(NULL, FALSE, NULL);

    if (mutexVerificador == NULL) return 1;

    mutexPonte = CreateMutex(NULL, FALSE, NULL);

    if (mutexPonte == NULL) return 1;

    eventoPonteLivre = CreateEvent(NULL, TRUE, TRUE, NULL);

    if (eventoPonteLivre == NULL) {
        CloseHandle(mutexPonte);

        return 1;
    }

    semaforoCapacidade = CreateSemaphore(NULL, MAX_PONTE, MAX_PONTE, NULL);

    if (semaforoCapacidade == NULL) return 1;

    for (i = 0; i <2; i++){
        mutexSentido[i] = CreateMutex(NULL, FALSE, NULL);

        if (mutexSentido[i] == NULL) return 1;
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

    CloseHandle(mutexPonte);

    CloseHandle(eventoPonteLivre);

    for(i = 0; i < 2; i++){
        CloseHandle(mutexSentido[i]);
    }

        CloseHandle(mutexVerificador);

    mostrarEspera(NORTE, totalCarros);
    mostrarEspera(SUL, totalCarros);
    printf("Debito: %d carro/s por segundo \n", debito);
    printf("Total de violacoes: %d\n", violacoes);

    return 0;
}
