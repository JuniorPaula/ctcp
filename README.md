# Servidor de Chat em C

Este é um projeto de servidor de chat em C que implementa um serviço de comunicação TCP para múltiplos clientes. O servidor permite que clientes conectados enviem mensagens que são retransmitidas para todos os outros clientes. O sistema também possui funcionalidades de limite de mensagens, banimento de clientes e registro de logs em um arquivo de texto.

## Funcionalidades

- **Conexão e Desconexão de Clientes**: O servidor aceita múltiplas conexões simultâneas e notifica a conexão e desconexão de cada cliente.
- **Envio de Mensagens**: Mensagens enviadas por um cliente são recebidas pelo servidor e retransmitidas para todos os outros clientes conectados.
- **Controle de Taxa de Mensagens**: Cada cliente tem um limite de envio de uma mensagem por segundo. Se exceder o limite, um "strike" é contado.
- **Sistema de Banimento**: Após atingir o limite de "strikes" (infrações), o cliente é banido por um período determinado.
- **Registro de Logs em Arquivo**: Todas as ações relevantes, como conexões, desconexões, mensagens enviadas e banimentos, são registradas em um arquivo de log (`chat_log.txt`) com timestamps.

## Estrutura do Código

### Principais Componentes

- **main()**: Configura o servidor, inicializa o arquivo de log e entra em um loop para aceitar conexões de clientes.
- **write_log()**: Função que encapsula a escrita de logs em um arquivo, garantindo o uso seguro com mutex para evitar problemas de concorrência.
- **client_thread()**: Função que lida com a comunicação com cada cliente em uma thread separada, recebendo mensagens e enviando-as ao servidor.
- **server_thread()**: Processa mensagens recebidas de clientes e distribui as mensagens para todos os clientes conectados.

### Estruturas e Constantes Importantes

- **Client**: Representa um cliente conectado, com detalhes de conexão, timestamp da última mensagem, número de "strikes" e endereço IP.
- **MessageType**: Enumeração que define os tipos de mensagens processados pelo servidor, como conexão, desconexão e mensagens de chat.
- **MessageQueue**: Estrutura que armazena as mensagens em uma fila protegida por mutex, permitindo comunicação entre threads de clientes e o servidor.

## Configuração e Uso

### Requisitos

- GCC (ou outro compilador C compatível com POSIX)
- Sistema operacional que suporte programação em sockets e threads (Linux, macOS, etc.)

### Compilação

Para compilar o servidor, execute:

```bash
gcc -o server server.c -pthread
