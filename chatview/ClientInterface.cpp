/* 
 * Copyright (C) 2024 Petr Mironychev
 *
 * This file is part of QodeAssist.
 *
 * QodeAssist is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * QodeAssist is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with QodeAssist. If not, see <https://www.gnu.org/licenses/>.
 */

#include "ClientInterface.hpp"

#include <QJsonArray>
#include <QJsonDocument>
#include <QUuid>

#include "ChatAssistantSettings.hpp"
#include "GeneralSettings.hpp"
#include "Logger.hpp"
#include "PromptTemplateManager.hpp"
#include "ProvidersManager.hpp"

namespace QodeAssist::Chat {

ClientInterface::ClientInterface(ChatModel *chatModel, QObject *parent)
    : QObject(parent)
    , m_requestHandler(new LLMCore::RequestHandler(this))
    , m_chatModel(chatModel)
{
    connect(m_requestHandler,
            &LLMCore::RequestHandler::completionReceived,
            this,
            [this](const QString &completion, const QJsonObject &request, bool isComplete) {
                handleLLMResponse(completion, request, isComplete);
            });

    connect(m_requestHandler,
            &LLMCore::RequestHandler::requestFinished,
            this,
            [this](const QString &, bool success, const QString &errorString) {
                if (!success) {
                    emit errorOccurred(errorString);
                }
            });
}

ClientInterface::~ClientInterface() = default;

void ClientInterface::sendMessage(const QString &message)
{
    cancelRequest();

    auto &chatAssistantSettings = Settings::chatAssistantSettings();

    auto providerName = Settings::generalSettings().caProvider();
    auto provider = LLMCore::ProvidersManager::instance().getProviderByName(providerName);

    auto templateName = Settings::generalSettings().caTemplate();
    auto promptTemplate = LLMCore::PromptTemplateManager::instance().getChatTemplateByName(
        templateName);

    LLMCore::ContextData context;
    context.prefix = message;
    context.suffix = "";
    if (chatAssistantSettings.useSystemPrompt())
        context.systemPrompt = chatAssistantSettings.systemPrompt();

    QJsonObject providerRequest;
    providerRequest["model"] = Settings::generalSettings().caModel();
    providerRequest["stream"] = true;
    providerRequest["messages"] = m_chatModel->prepareMessagesForRequest(context);

    if (promptTemplate)
        promptTemplate->prepareRequest(providerRequest, context);
    else
        qWarning("No prompt template found");

    if (provider)
        provider->prepareRequest(providerRequest, LLMCore::RequestType::Chat);
    else
        qWarning("No provider found");

    LLMCore::LLMConfig config;
    config.requestType = LLMCore::RequestType::Chat;
    config.provider = provider;
    config.promptTemplate = promptTemplate;
    config.url = QString("%1%2").arg(Settings::generalSettings().caUrl(), provider->chatEndpoint());
    config.providerRequest = providerRequest;
    config.multiLineCompletion = false;

    QJsonObject request;
    request["id"] = QUuid::createUuid().toString();

    m_chatModel->addMessage(message, ChatModel::ChatRole::User, "");
    m_requestHandler->sendLLMRequest(config, request);
}

void ClientInterface::clearMessages()
{
    m_chatModel->clear();
    LOG_MESSAGE("Chat history cleared");
}

void ClientInterface::cancelRequest()
{
    auto id = m_chatModel->lastMessageId();
    m_requestHandler->cancelRequest(id);
}

void ClientInterface::handleLLMResponse(const QString &response,
                                        const QJsonObject &request,
                                        bool isComplete)
{
    QString messageId = request["id"].toString();
    m_chatModel->addMessage(response.trimmed(), ChatModel::ChatRole::Assistant, messageId);

    if (isComplete) {
        LOG_MESSAGE("Message completed. Final response for message " + messageId + ": " + response);
    }
}

} // namespace QodeAssist::Chat
