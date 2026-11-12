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

LONG carrosNaPonte[2] = {0, 0};
LONG violacoes = 0;

//protege a verificção dos contadores
HANDLE mutexVerificador;

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

void ponte_entrar(Carro *carro){
    verificar(carro, 1);

}

void ponte_sair(Carro *carro){
    verificar(carro, 0);
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

    if(argc != 5 && argc != 6){
        printf("Uso: ponte.exe <n_norte> <n_sul> <chegada_ms> <travessia_ms> [semente]");
        return 1;
    }

    nNorte = atoi(argv[1]);
    nSul = atoi(argv[2]);
    intervaloChegada = atoi(argv[3]);
    passagem = atoi(argv[4]);

    //verificar quantidade de carros de cada lado
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

    //mutex do verificador
    mutexVerificador = CreateMutex(NULL, FALSE, NULL);

    if (mutexVerificador == NULL) {
        printf("Erro ao criar o mutex do verificador.\n");
        return 1;
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

    // Fecha os handles das threads ja terminadas
    for (i = 0; i < totalCarros; i++) {
        if (!CloseHandle(threads[i])) {
            printf("Erro ao fechar o handle da thread.\n");
            return 1;
        }
    }


    if (!CloseHandle(mutexVerificador)) {
        printf("Erro ao fechar o handle do mutex.\n");
        return 1;
    }

    printf("Total de violacoes: %ld\n", violacoes);

    return 0;
}